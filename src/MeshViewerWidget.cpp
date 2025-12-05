#include "MeshViewerWidget.h"

#include <QMatrix4x4>
#include <QtMath>
#include <cmath>

MeshViewerWidget::MeshViewerWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(480, 360);
    setFocusPolicy(Qt::StrongFocus);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, [this]() {
        m_rotationY = std::fmod(m_rotationY + 0.25f, 360.0f);
        update();
    });
    m_timer->start(16); // ~60 FPS idle spin for a bit of life
    m_cameraDistance = 5.0f; // Good default view
}

MeshViewerWidget::~MeshViewerWidget()
{
    makeCurrent();
    m_vao.destroy();
    m_vbo.destroy();
    m_program.release();
    doneCurrent();
}

void MeshViewerWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);

    const char *vs = R"(#version 430 core
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

uniform mat4 u_mvp;
uniform mat4 u_model; // To World Space

out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    // Pass attributes to fragment shader in World Space
    vec4 worldPos = u_model * vec4(inPos, 1.0);
    vWorldPos = worldPos.xyz;
    
    // Normal Matrix should be inverse-transpose of top-left 3x3 of Model Matrix
    // If Model is just rotation, it's just the Model matrix rotation
    vNormal = mat3(u_model) * inNormal;
    
    gl_Position = u_mvp * vec4(inPos, 1.0);
})";

    const char *fs = R"(#version 430 core
in vec3 vNormal;
in vec3 vWorldPos;

out vec4 fragColor;

uniform bool u_wireframe;
uniform vec3 u_lightDir; // World Space Direction
uniform vec3 u_cameraPos; // World Space Camera

void main() {
    vec3 n = normalize(vNormal);
    vec3 l = normalize(u_lightDir);
    vec3 v = normalize(u_cameraPos - vWorldPos);
    vec3 h = normalize(l + v);

    // Standard Phong
    float diff = max(dot(n, l), 0.0);
    float spec = pow(max(dot(n, h), 0.0), 32.0);
    
    // Colors
    vec3 ambientColor = vec3(0.1, 0.1, 0.15); // Slight blue tint
    vec3 diffuseColor = vec3(0.2, 0.5, 0.8); // Blue sphere
    vec3 specColor = vec3(1.0, 1.0, 1.0);
    
    vec3 result = ambientColor + diff * diffuseColor + spec * specColor;

    if (u_wireframe) {
        fragColor = vec4(vec3(0.08), 1.0);
    } else {
        fragColor = vec4(result, 1.0);
    }
})";

    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vs)) {
        qWarning() << "Vertex shader compile error:" << m_program.log();
    }
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fs)) {
        qWarning() << "Fragment shader compile error:" << m_program.log();
    }
    if (!m_program.link()) {
        qWarning() << "Shader link error:" << m_program.log();
    }

    buildSphere();
    uploadMesh();

    qDebug() << "MeshViewerWidget::initializeGL - Success. Mesh vertices:" << m_vertexCount;
}

void MeshViewerWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void MeshViewerWidget::paintGL()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    
    // Diagnostic Background: Light Grey to see outline if object is dark
    glClearColor(0.2f, 0.2f, 0.25f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_vertexCount == 0) {
        return;
    }

    QMatrix4x4 model;
    model.rotate(-20.0f, 1.0f, 0.0f, 0.0f); // Tilt
    model.rotate(m_rotationY, 0.0f, 1.0f, 0.0f); // Spin

    QMatrix4x4 view;
    // Camera is at (0,0,dist) looking at (0,0,0)
    // So we translate Scene by -dist
    view.translate(0.0f, 0.0f, -m_cameraDistance);

    QMatrix4x4 proj;
    const float aspect = width() > 0 ? float(width()) / float(height()) : 1.0f;
    proj.perspective(45.0f, aspect, 0.1f, 100.0f);

    QMatrix4x4 mvp = proj * view * model;

    m_program.bind();
    m_program.setUniformValue("u_mvp", mvp);
    m_program.setUniformValue("u_model", model);
    m_program.setUniformValue("u_wireframe", false);
    m_program.setUniformValue("u_lightDir", m_lightDir);
    // Camera Position in World Space is (0,0,dist) * Inverse(ViewRotation) ... 
    // Wait, simpler: Camera is at (0,0,dist) relative to the object's origin if we moved object away.
    // If we define View as translation only, Camera is effectively at (0,0, +m_cameraDistance).
    m_program.setUniformValue("u_cameraPos", QVector3D(0.0f, 0.0f, m_cameraDistance));

    m_vao.bind();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);

    if (m_showWireframe) {
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.0f, -1.0f);
        glLineWidth(1.0f);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        m_program.setUniformValue("u_wireframe", true);
        glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_POLYGON_OFFSET_LINE);
    }

    m_vao.release();
    m_program.release();
}

void MeshViewerWidget::buildSphere()
{
    // Analytical Sphere Generation
    // We don't rely on OpenMesh for normals to avoid artifacts at seams/poles
    m_vertices.clear();
    
    constexpr float pi = 3.14159265359f;
    const int slices = 64; // Higher res
    const int stacks = 64;
    const float radius = 1.0f;
    
    // We generate triangles directly
    for (int i = 0; i < stacks; ++i) {
        float lat0 = pi * (-0.5f + (float)(i) / stacks);
        float z0 = std::sin(lat0);
        float zr0 = std::cos(lat0);
        
        float lat1 = pi * (-0.5f + (float)(i+1) / stacks);
        float z1 = std::sin(lat1);
        float zr1 = std::cos(lat1);
        
        for (int j = 0; j < slices; ++j) {
            float lng0 = 2 * pi * (float)(j) / slices;
            float x0 = std::cos(lng0);
            float y0 = std::sin(lng0);
            
            float lng1 = 2 * pi * (float)(j+1) / slices;
            float x1 = std::cos(lng1);
            float y1 = std::sin(lng1);
            
            // Quad vertices
            QVector3D p0(x0 * zr0, y0 * zr0, z0); // Bottom Left
            QVector3D p1(x1 * zr0, y1 * zr0, z0); // Bottom Right
            QVector3D p2(x0 * zr1, y0 * zr1, z1); // Top Left
            QVector3D p3(x1 * zr1, y1 * zr1, z1); // Top Right
            
            // Normals are just the positions normalized (for unit sphere)
            // But we scaled by radius? Wait, logic above produces radius 1
            // If we want actual radius, scale pos. Normal stays same.
            
            QVector3D n0 = p0.normalized();
            QVector3D n1 = p1.normalized();
            QVector3D n2 = p2.normalized();
            QVector3D n3 = p3.normalized();
            
            // Triangle 1 (p0, p1, p2)
            m_vertices.push_back({p0 * radius, n0});
            m_vertices.push_back({p1 * radius, n1});
            m_vertices.push_back({p2 * radius, n2});
            
            // Triangle 2 (p2, p1, p3)
            m_vertices.push_back({p2 * radius, n2});
            m_vertices.push_back({p1 * radius, n1});
            m_vertices.push_back({p3 * radius, n3});
        }
    }

    m_vertexCount = static_cast<int>(m_vertices.size());
    qDebug() << "Built analytical sphere with" << m_vertexCount << "vertices";
}

void MeshViewerWidget::uploadMesh()
{
    if (m_vertices.empty())
        return;

    m_vao.create();
    m_vao.bind();

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(m_vertices.data(), static_cast<int>(m_vertices.size() * sizeof(Vertex)));

    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, offsetof(Vertex, position), 3, sizeof(Vertex));
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, offsetof(Vertex, normal), 3, sizeof(Vertex));

    m_vbo.release();
    m_vao.release();
}

void MeshViewerWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_W || event->key() == Qt::Key_P) {
        m_showWireframe = !m_showWireframe;
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void MeshViewerWidget::setLightDirection(const QVector3D &dir)
{
    m_lightDir = dir.normalized();
    update();
}

void MeshViewerWidget::setCameraDistance(float dist)
{
    m_cameraDistance = dist;
    update();
}
