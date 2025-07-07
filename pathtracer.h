#ifndef PATHTRACER_H
#define PATHTRACER_H

#include <QImage>

#include "scene/scene.h"

struct Settings {
    int samplesPerPixel;
    bool directLightingOnly; // if true, ignore indirect lighting
    int numDirectLightingSamples; // number of shadow rays to trace from each intersection point
    float pathContinuationProb; // probability of spawning a new secondary ray == (1-pathTerminationProb)
};

class PathTracer
{
public:
    PathTracer(int width, int height);

    void traceScene(QRgb *imageData, const Scene &scene);
    Settings settings;

private:
    int m_width, m_height;

    void toneMap(QRgb *imageData, std::vector<Eigen::Vector3f> &intensityValues);

    float luminance(Eigen::Vector3f rgb);

    Eigen::Vector3f tracePixel(int x, int y, const Scene &scene, const Eigen::Matrix4f &invViewMatrix, float light_area);
    Eigen::Vector3f traceRay(const Ray& r, const Scene &scene, float light_area, bool count_emitted);
    Eigen::Vector3f sphericalToCartesian(float theta, float phi);
    Eigen::Vector3f get_brdf(Eigen::Vector3f diffuse, Eigen::Vector3f specular, Eigen::Vector3f normal, Eigen::Vector3f reflected, Eigen::Vector3f outgoing, float shine);
    Eigen::Vector3f reflected_ray(Eigen::Vector3f normal, Eigen::Vector3f incoming);
    Eigen::Vector3f incoming_minus_proj(Eigen::Vector3f normal, Eigen::Vector3f incoming);
    Eigen::Vector3f refracted_ray(Eigen::Vector3f normal, Eigen::Vector3f incoming, float n1, float n2);
    Eigen::Vector3f sample_rand_dir(Eigen::Vector3f normal);
    Eigen::Vector3f change_luminance(Eigen::Vector3f pix_in, float l_in, float l_out);
    float reinhard_extended(float lum, float max_white);
};

#endif // PATHTRACER_H
