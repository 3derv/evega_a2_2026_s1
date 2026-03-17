#ifndef VECTOR3_H
#define VECTOR3_H

#include <cmath>
#include <iostream>

// Vector3: Representa un vector 3D con operaciones matemáticas básicas.
// Utilizado para coordenadas 3D, colores RGB y direcciones.
struct Vector3 {
    double x, y, z;

    Vector3() : x(0), y(0), z(0) {}  // Constructor por defecto (origen)
    Vector3(double x, double y, double z) : x(x), y(y), z(z) {}  // Constructor con valores

    // Operaciones vectoriales de suma y resta
    Vector3 operator+(const Vector3& v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
    Vector3 operator-(const Vector3& v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
    
    // Escalado: multiplicación/división por escalar
    Vector3 operator*(double s) const { return Vector3(x * s, y * s, z * s); }
    Vector3 operator/(double s) const { return Vector3(x / s, y / s, z / s); }
    
    // dot(): Producto punto - mide similitud y ángulo entre vectores
    double dot(const Vector3& v) const { return x * v.x + y * v.y + z * v.z; }
    
    // cross(): Producto cruz - genera vector perpendicular a ambos
    Vector3 cross(const Vector3& v) const {
        return Vector3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
    }
    
    // length(): Longitud (magnitud) del vector
    double length() const { return sqrt(dot(*this)); }
    
    // normalize(): Retorna vector con longitud 1 (mantiene dirección)
    Vector3 normalize() const { return *this / length(); }
};

#endif // VECTOR3_H