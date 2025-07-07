#include "pathtracer.h"

#include <iostream>

#include <random>

#include <numbers>

#include <cmath>

#include <Eigen/Dense>

#include <util/CS123Common.h>

using namespace Eigen;

PathTracer::PathTracer(int width, int height)
    : m_width(width), m_height(height)
{
}

void PathTracer::traceScene(QRgb *imageData, const Scene& scene)
{
    float total_light_area = 0;
    foreach(Triangle *tri, scene.getEmissives()){
        //find two vectors connecting the first vertex to the other two
        Vector3f AB = tri->getVertices()[1] - tri->getVertices()[0];
        Vector3f AC = tri->getVertices()[2] - tri->getVertices()[0];
        //take the cross proudct of those areas which has magnitude equal to the parallelogram formed by the two vectors
        //since we want the triangle area we divide it by 2
        tri->setArea(AB.cross(AC).norm()/2.f);
        total_light_area += tri->getArea();
    }
    std::vector<Vector3f> intensityValues(m_width * m_height);
    Matrix4f invViewMat = (scene.getCamera().getScaleMatrix() * scene.getCamera().getViewMatrix()).inverse();
    for(int y = 0; y < m_height; ++y) {
        //#pragma omp parallel for
        for(int x = 0; x < m_width; ++x) {
            int offset = x + (y * m_width);
            std::cout<<offset<<std::endl;
            intensityValues[offset] = tracePixel(x, y, scene, invViewMat, total_light_area);
        }
    }

    toneMap(imageData, intensityValues);
}

std::random_device rd;  // Non-deterministic random source
std::mt19937 gen(rd()); // Mersenne Twister engine
std::uniform_real_distribution<double> dist(0.0, 1.0); // Uniform distribution between 0 and 1
Vector3f PathTracer::tracePixel(int x, int y, const Scene& scene, const Matrix4f &invViewMatrix, float light_area)
{
    Vector3f p(0, 0, 0);
    Vector3f pix_color(0, 0, 0);
    for(int i = 0; i < settings.samplesPerPixel; i++){
        Vector3f d((2.f * (x + dist(gen)) / m_width) - 1, 1 - (2.f * (y + dist(gen)) / m_height), -1);
        d.normalize();

        Ray r(p, d);
        r = r.transform(invViewMatrix);
        pix_color += traceRay(r,scene, light_area, true);
    }
    return pix_color/settings.samplesPerPixel;
}

Vector3f PathTracer::traceRay(const Ray& r, const Scene& scene, float totalLightArea, bool count_emitted)
{
    IntersectionInfo i;
    Ray ray(r);
    Vector3f L = {0,0,0};//this is the total color from the light which starts out as just black
    //check if ray intersects with anything
    if(scene.getIntersection(ray, &i)) {
          //** Example code for accessing materials provided by a .mtl file **
        const Triangle *t = static_cast<const Triangle *>(i.data);//Get the triangle in the mesh that was intersected
        const tinyobj::material_t& mat = t->getMaterial();//Get the material of the triangle from the mesh
        const tinyobj::real_t *e = mat.emission;//Emission color as array of floats
        const tinyobj::real_t *d = mat.diffuse;//Diffuse color as array of floats
        const tinyobj::real_t *s = mat.specular;//Specular color as array of floats
        const std::string diffuseTex = mat.diffuse_texname;//Diffuse texture name
        float shine = mat.shininess;
        float n = mat.ior;
        Vector3f emission = {e[0], e[1], e[2]};
        Vector3f diffuse = {d[0],d[1],d[2]};
        Vector3f specular = {s[0], s[1], s[2]};

        Vector3f hit = i.hit;//this is the positon where the ray intersects
        //surface normal of where we intersected the object
        Vector3f surf_norm = i.object->getNormal(i);
        //reflected ray which will only be calculated under conditions below
        Vector3f perf_refl = {0.f, 0.f, 0.f};
        //we want to calculate and store the reflected ray if we have a specular reflection
        //this way we only have to calculate it once per ray
        if(mat.illum == 5 || specular.x() > 0.5f || specular.y() > 0.5f || specular.z() > 0.5f){
            perf_refl = reflected_ray(surf_norm, r.d);
        }

        //we should only do direct lighting if we aren't counting the emitted light
        if(settings.directLightingOnly || (mat.illum != 5 && mat.illum != 7)){
            //in this scene the two emissive triangles are the same size so the probability of sampling anypoint is 1/the total area of the lights
            float lightSampleProb = 1.f/totalLightArea;
            //here we sample a random triangle from the emissive triangles
            Vector3f totalLight;
            Vector3f directL(0, 0, 0);
            for(int i = 0; i < settings.numDirectLightingSamples/2.f; i++){
                for(Triangle *light:scene.getEmissives()){
                    std::vector<Vector3f> random_pts;
                    //for the number of direct lighting samples
                    //sample random point within the triangle
                    float alpha = dist(gen);
                    std::uniform_real_distribution<double> beta_dist(0.f, 1.f-alpha);
                    float beta = beta_dist(gen);
                    float gamma = 1-(alpha+beta);
                    Vector3f random_pt = (alpha * light->getVertices()[0]) + (beta * light->getVertices()[1]) + (gamma * light->getVertices()[2]);


                    //the direction pointing to the light source
                    Vector3f dir_to_light = random_pt - hit;
                    float distance_from_light = dir_to_light.norm();
                    //normalize the direction
                    dir_to_light = dir_to_light.normalized();

                    //construct a ray from our current point to the random point on the light source
                    Ray shadow_ray(hit, dir_to_light);
                    IntersectionInfo shadow_i;
                    //here we want to make sure that we don't intersect with anything before the light source
                    if(scene.getIntersection(shadow_ray, &shadow_i)){
                        //this triangle is the first intersected with along the path
                        const Triangle *shadow_t = static_cast<const Triangle *>(shadow_i.data);

                        //the triangle needs to be the same as the light for the light to contribute it's emission, otherwise, the point is in shadow
                        if(shadow_t->getIndex() == light->getIndex()){
                            const tinyobj::material_t& light_mat = light->getMaterial();//Get the material of the light from the mesh
                            const tinyobj::real_t *light_e = light_mat.emission;//Emission color as array of floats
                            //store emission into a vector
                            Vector3f light_emission = {light_e[0], light_e[1], light_e[2]};
                            //also get the normal at the point we sampled
                            Vector3f light_norm = light->getNormal(shadow_i);

                            //get the brdf for our material
                            Vector3f brdf = get_brdf(diffuse, specular, surf_norm, perf_refl, dir_to_light, shine);
                            float light_cos = std::max(0.f, (-dir_to_light).dot(light_norm));
                            // std::cout<<"adding light"<<std::endl;
                            L += ((light_emission.cwiseProduct(brdf) * dir_to_light.dot(surf_norm) * light_cos)/(pow(distance_from_light, 2.f)))/(lightSampleProb * settings.numDirectLightingSamples);
                        }
                    }
                }


            }
        }
        //if we are counting the emitted light then add it to L
        if(count_emitted){
            L += emission;//add emission
        }

        if(!settings.directLightingOnly){


            float pdf_rr = settings.pathContinuationProb;
            if(dist(gen) < pdf_rr){
                //if refraction
                if(mat.illum == 7){
                    Vector3f refract_dir;
                    float probability_zero;
                    float probability;
                    //if the ray is exiting the refractive object
                    if(surf_norm.dot(r.d) > 0.f){
                        //first calculate probability of reflecantace if viewing angle is 0
                        probability_zero = pow(((mat.ior-1.f)/(mat.ior + 1.f)), 2.f);
                        //now calculate the probability according ot our current viewing angle
                        probability = probability_zero + ((1.f - probability_zero) * (1 - (-surf_norm).dot(-r.d)));
                        if(dist(gen) <= probability){
                            refract_dir = reflected_ray(-surf_norm, r.d);
                        }
                        else{
                            probability = 1.f - probability;
                            //in this case, we are going from high to low index so we pass in the mat.ior has index 1 and just 1 for index 2
                            //also we make the surf_norm negative since we are insidde the object
                            refract_dir = refracted_ray(-surf_norm, r.d, mat.ior, 1.f);
                        }

                    }
                    //if our ray is entering the object
                    else{
                        //first calculate probability of reflecantace if viewing angle is 0
                        probability_zero = pow(((1.f-mat.ior)/(mat.ior + 1.f)), 2.f);
                        //now calculate the probability according ot our current viewing angle
                        probability = probability_zero + ((1.f - probability_zero) * (1.f - (surf_norm).dot(-r.d)));
                        if(dist(gen) <= probability){
                            refract_dir = reflected_ray(-surf_norm, r.d);
                        }
                        else{
                            probability = 1.f - probability;
                            //vice versa
                            refract_dir = refracted_ray(surf_norm, r.d, 1.f, mat.ior);
                        }
                    }
                    Ray refract_ray (hit, refract_dir);
                    L += (traceRay(refract_ray, scene, totalLightArea, true))/(pdf_rr);
                }
                else{
                    //if perfect reflection
                    if(mat.illum == 5){
                        //outgoing ray is just perfect reflection about normal
                        Ray reflection_ray (hit, perf_refl);
                        L += (traceRay(reflection_ray, scene, totalLightArea, true))/pdf_rr;
                    }
                    else{
                        Vector3f outgoing = sample_rand_dir(surf_norm);
                        //probability of sampling any direciton is 1/2*pi since we sample uniformally around the hemisphere
                        float pdf = 1/(2 * std::numbers::pi);
                        Vector3f brdf = get_brdf(diffuse, specular, surf_norm, perf_refl, outgoing, shine);
                        //create a ray going out in rand dir
                        Ray r_out(hit, outgoing);
                        L += (traceRay(r_out, scene, totalLightArea, false).cwiseProduct(brdf) * outgoing.dot(surf_norm))/(pdf * pdf_rr);
                    }
                }
            }
        }

    }
    return L;
}


void PathTracer::toneMap(QRgb *imageData, std::vector<Vector3f> &intensityValues) {
    for(int y = 0; y < m_height; ++y) {
        for(int x = 0; x < m_width; ++x) {
            int offset = x + (y * m_width);
            float r = intensityValues[offset].x();
            float g = intensityValues[offset].y();
            float b = intensityValues[offset].z();
            float gamma = 1.f/2.2f;
            r = pow(r/(1+r), gamma);
            g = pow(g/(1+g), gamma);
            b = pow(b/(1+b), gamma);

            imageData[offset] = qRgb(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255));
        }
    }
}

Vector3f PathTracer::sphericalToCartesian(float theta, float phi){
    float x = std::sin(theta) * std::cos(phi);
    float y = std::sin(theta) * std::sin(phi);
    float z = std::cos(theta);
    return Vector3f(x, y, z);
}

//returns the combined brdf
Vector3f PathTracer::get_brdf(Vector3f diffuse, Vector3f specular, Vector3f normal, Vector3f reflected, Vector3f outgoing, float shine){
    //if our specular term is majority term
    if(specular.x() > 0.5f || specular.y() > 0.5f || specular.z() > 0.5f){
            //find dot product between the outgoing ray with the reflected ray
            float dotted_with_outgoing = outgoing.dot(reflected);
            float final_spec = ((shine + 2)/(2 * std::numbers::pi)) * (pow(dotted_with_outgoing, shine));
            //this vector is the specular terms in final form
            return final_spec * specular;

    }
    else{
        return diffuse/std::numbers::pi;
    }
}

Vector3f PathTracer::refracted_ray(Vector3f normal, Vector3f incoming, float n1, float n2){
    //this vector is the projection of the incoming ray onto the normal
    Vector3f proj_on_norm = (-incoming).dot(normal) * normal;
    //the
    Vector3f proj_to_incoming = (-incoming) - proj_on_norm;
    //the is the horizontal component of the refracted ray
    Vector3f horiz_comp = -(n1/n2) * proj_to_incoming;
    //check if we're at a critical angle
    if(horiz_comp.norm() <= 1){
        //here we're not at critical angle so we calculate the vertical component using pythagreom theorem
        Vector3f vertical_comp = -(sqrt(1.f - pow(horiz_comp.norm(), 2.f))) * normal;
        return vertical_comp + horiz_comp;
    }
    else{
        //if we're past critical angle we should just return a reflected ray
        return incoming + (2.f * proj_on_norm);
    }
}

//returns perfect reflection of incoming about normal
Vector3f PathTracer::reflected_ray(Vector3f normal, Vector3f incoming){
    //this vector is the projection of the incoming ray onto the normal
    Vector3f proj_on_norm = (-incoming).dot(normal) * normal;
    //this vector is the reflected ray
    Vector3f reflected = incoming + (2.f * proj_on_norm);
    return reflected;
}



//samples a random direction with the sampling hemisphere centered around the normal
Vector3f PathTracer::sample_rand_dir(Vector3f normal){
    //compute random theta and phi
    float phi = (2.f * std::numbers::pi * dist(gen));
    float theta = (std::acos(dist(gen)));
    //finally we have our random direction
    Vector3f randDir = sphericalToCartesian(theta, phi).normalized();

    //create a quaternion that takes oyu from the z axis to the surface normal
    Quaternion R = Quaternionf().setFromTwoVectors(Vector3f(0,0,1), normal);
    //use it to transform our random direction
    return R * randDir;
}


