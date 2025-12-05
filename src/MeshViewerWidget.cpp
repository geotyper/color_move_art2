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
uniform mat3 u_normalMatrix;

out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec4 worldPos = vec4(inPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(u_normalMatrix * inNormal);
    gl_Position = u_mvp * worldPos;
})";

    const char *fs = R"(#version 430 core
in vec3 vNormal;
in vec3 vWorldPos;

out vec4 fragColor;

uniform bool u_wireframe;
uniform vec3 u_lightDir;
uniform vec3 u_cameraPos;

void main() {
    vec3 n = normalize(vNormal);
    vec3 l = normalize(u_lightDir);
    vec3 v = normalize(u_cameraPos - vWorldPos);
    vec3 h = normalize(l + v);

    float diff = max(dot(n, l), 0.0);
    float spec = pow(max(dot(n, h), 0.0), 32.0);
    float ambient = 0.2;
    vec3 baseColor = vec3(0.35, 0.75, 1.0);
    vec3 lit = baseColor * (ambient + diff) + vec3(0.25) * spec;

    if (u_wireframe) {
        fragColor = vec4(vec3(0.08), 1.0);
    } else {
        fragColor = vec4(lit, 1.0);
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
    
    // Diagnostic Background: Dark Blue-ish to differentiate from Black
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_vertexCount == 0) {
        return;
    }

    QMatrix4x4 model;
    model.rotate(-20.0f, 1.0f, 0.0f, 0.0f);
    model.rotate(m_rotationY, 0.0f, 1.0f, 0.0f);

    QMatrix4x4 view;
    view.translate(0.0f, 0.0f, -3.0f);

    QMatrix4x4 proj;
    const float aspect = width() > 0 ? float(width()) / float(height()) : 1.0f;
    proj.perspective(45.0f, aspect, 0.1f, 15.0f);

    QMatrix4x4 mvp = proj * view * model;
    QMatrix3x3 normalMat = (view * model).normalMatrix();

    m_program.bind();
    m_program.setUniformValue("u_mvp", mvp);
    m_program.setUniformValue("u_normalMatrix", normalMat);
    m_program.setUniformValue("u_wireframe", false);
    m_program.setUniformValue("u_lightDir", QVector3D(0.3f, 0.7f, 0.4f).normalized());
    m_program.setUniformValue("u_cameraPos", QVector3D(0.0f, 0.0f, 3.0f));

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
    Mesh mesh;
    constexpr float pi = 3.14159265359f;
    const int slices = 32;
    const int stacks = 32;
    const float radius = 1.0f;

    std::vector<std::vector<Mesh::VertexHandle>> vh(stacks + 1, std::vector<Mesh::VertexHandle>(slices + 1));

    for (int stack = 0; stack <= stacks; ++stack) {
        float v = float(stack) / float(stacks);
        float phi = (-0.5f + v) * pi; // -90..90 deg
        float y = radius * std::sin(phi);
        float r = radius * std::cos(phi);

        for (int slice = 0; slice <= slices; ++slice) {
            float u = float(slice) / float(slices);
            float theta = u * (pi * 2.0f);
            float x = r * std::cos(theta);
            float z = r * std::sin(theta);

            vh[stack][slice] = mesh.add_vertex(Mesh::Point(x, y, z));
        }
    }

    for (int stack = 0; stack < stacks; ++stack) {
        for (int slice = 0; slice < slices; ++slice) {
            auto v00 = vh[stack][slice];
            auto v01 = vh[stack][slice + 1];
            auto v10 = vh[stack + 1][slice];
            auto v11 = vh[stack + 1][slice + 1];

            if (!mesh.add_face(v00, v10, v11).is_valid()) {
                // qWarning("Failed to add face (v00, v10, v11)");
            }
            if (!mesh.add_face(v00, v11, v01).is_valid()) {
                // qWarning("Failed to add face (v00, v11, v01)");
            }
        }
    }

    mesh.request_vertex_normals();
    mesh.update_normals();

    m_vertices.clear();
    m_vertices.reserve(mesh.n_faces() * 3);
    for (const auto &face : mesh.faces()) {
        for (const auto &v : mesh.fv_range(face)) {
            const auto &p = mesh.point(v);
            const auto &n = mesh.normal(v);
            m_vertices.push_back({QVector3D(p[0], p[1], p[2]), QVector3D(n[0], n[1], n[2])});
        }
    }

    if (m_vertices.empty()) {
        qWarning() << "Sphere generation failed or empty. Using fallback cube.";
        // Fallback cube if OpenMesh sphere creation failed for any reason
        const QVector3D normals[] = {
            { 1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0,-1, 0}, {0,0, 1}, {0,0,-1}
        };
        const QVector3D positions[][4] = {
            {{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},   // +X
            {{-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}}, // -X
            {{-1, 1, -1}, {1, 1, -1}, {1, 1, 1}, {-1, 1, 1}},   // +Y
            {{-1, -1, 1}, {1, -1, 1}, {1, -1, -1}, {-1, -1, -1}}, // -Y
            {{-1, -1, 1}, {-1, 1, 1}, {1, 1, 1}, {1, -1, 1}},   // +Z
            {{1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, -1}} // -Z
        };
        for (int f = 0; f < 6; ++f) {
            const auto &n = normals[f];
            const auto *p = positions[f];
            m_vertices.push_back({p[0], n});
            m_vertices.push_back({p[1], n});
            m_vertices.push_back({p[2], n});
            m_vertices.push_back({p[0], n});
            m_vertices.push_back({p[2], n});
            m_vertices.push_back({p[3], n});
        }
    }

    m_vertexCount = static_cast<int>(m_vertices.size());
    qDebug() << "Built mesh with" << m_vertexCount << "vertices";
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
