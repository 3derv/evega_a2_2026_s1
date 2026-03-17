#ifndef SPHERE_H
#define SPHERE_H

#include "Vector3.h"
#include "Ray.h"

struct Sphere {
    Vector3 center;
    double radius;
    Vector3 color;

    Sphere(const Vector3& c, double r, const Vector3& col) : center(c), radius(r), color(col) {}

    bool intersect(const Ray& ray, double& t) const {
        Vector3 oc = ray.origin - center;
        double a = ray.direction.dot(ray.direction);
        double b = 2.0 * oc.dot(ray.direction);
        double c = oc.dot(oc) - radius * radius;
        double discriminant = b * b - 4 * a * c;
        if (discriminant < 0) return false;
        t = (-b - sqrt(discriminant)) / (2 * a);
        return t > 0;
    }
};

#endif // SPHERE_H