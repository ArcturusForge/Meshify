// P4 step 2: draws the clustered mesh with Qt RHI and measures the whole path
// from a brush edit to finished GPU work.
//
//   meshify_p4_viewer [--vertices N] [--api opengl] [--size WxH]
//       Interactive: left mouse sculpts (shift digs in), right mouse orbits,
//       wheel zooms. The window title shows the cost of the last edit.
//   meshify_p4_viewer --shot out.png [--bench]   saves a picture first
//   meshify_p4_viewer --bench [--vertices N] [--edits N] [--api ...]
//       Scripted edits, prints a table and exits. Exit code 1 if the 95th
//       percentile from edit to finished GPU work exceeds 50 ms for any brush.
#include "cluster_mesh.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QRhiWidget>
#include <QWheelEvent>
#include <rhi/qrhi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>

using namespace meshify::p4;
using Clock = std::chrono::steady_clock;

namespace {

QShader loadShader(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QShader::fromSerialized(f.readAll());
}

double msBetween(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

double percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5))];
}

class Viewport : public QRhiWidget {
public:
    explicit Viewport(ClusterMesh& mesh) : m_mesh(mesh) { setMinimumSize(320, 240); }

    // Applies a brush edit and queues the changed ranges for the next frame.
    EditResult edit(const Vec3& center, float radius, float depth) {
        const EditResult r = m_mesh.brush(center, radius, depth);
        m_pending.insert(m_pending.end(), r.dirtyRanges.begin(), r.dirtyRanges.end());
        m_lastEdit = r;
        return r;
    }

    void forceFullUpload() { m_needFullUpload = true; }

    QString backendName() const { return m_backend; }
    QString deviceName() const { return m_device; }

protected:
    void initialize(QRhiCommandBuffer*) override {
        if (m_rhi != rhi()) {
            releaseResources();
            m_rhi = rhi();
            m_backend = QString::fromLatin1(rhi()->backendName());
            m_device = QString::fromUtf8(rhi()->driverInfo().deviceName);
        }
        if (m_pipeline) return;

        const size_t vbytes = m_mesh.render().size() * sizeof(RenderVertex);
        m_vbuf.reset(rhi()->newBuffer(QRhiBuffer::Static, QRhiBuffer::VertexBuffer, static_cast<quint32>(vbytes)));
        m_vbuf->create();

        // Triangle indices point into the cluster-ordered render buffer. They never change with edits.
        m_indices.clear();
        m_indices.reserve(m_mesh.triangleCount() * 3);
        for (const Cluster& c : m_mesh.clusters()) {
            for (uint32_t k = 0; k < c.triangleCount * 3; ++k) {
                m_indices.push_back(c.vertexOffset + m_mesh.clusterTriangles()[c.triangleOffset + k]);
            }
        }
        m_indexCount = static_cast<quint32>(m_indices.size());
        m_ibuf.reset(rhi()->newBuffer(QRhiBuffer::Static, QRhiBuffer::IndexBuffer,
                                      static_cast<quint32>(m_indices.size() * sizeof(uint32_t))));
        m_ibuf->create();
        m_needFullUpload = true;

        m_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
        m_ubuf->create();
        m_srb.reset(rhi()->newShaderResourceBindings());
        m_srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubuf.get())});
        m_srb->create();

        m_pipeline.reset(rhi()->newGraphicsPipeline());
        m_pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(":/shaders/mesh.vert.qsb")},
                                     {QRhiShaderStage::Fragment, loadShader(":/shaders/mesh.frag.qsb")}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{sizeof(RenderVertex)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}, {0, 1, QRhiVertexInputAttribute::Float3, 12}});
        m_pipeline->setVertexInputLayout(layout);
        m_pipeline->setShaderResourceBindings(m_srb.get());
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_pipeline->setDepthTest(true);
        m_pipeline->setDepthWrite(true);
        m_pipeline->setCullMode(QRhiGraphicsPipeline::Back);
        m_pipeline->setFrontFace(QRhiGraphicsPipeline::CCW);
        m_pipeline->setSampleCount(sampleCount());
        m_pipeline->create();
    }

    void render(QRhiCommandBuffer* cb) override {
        QRhiResourceUpdateBatch* u = rhi()->nextResourceUpdateBatch();
        if (m_needFullUpload) {
            u->uploadStaticBuffer(m_vbuf.get(), 0, static_cast<quint32>(m_mesh.render().size() * sizeof(RenderVertex)),
                                  m_mesh.render().data());
            u->uploadStaticBuffer(m_ibuf.get(), 0, static_cast<quint32>(m_indices.size() * sizeof(uint32_t)), m_indices.data());
            m_needFullUpload = false;
            m_pending.clear();  // the full upload already holds every edit so far
        } else {
            for (const auto& [first, count] : m_pending) {
                u->uploadStaticBuffer(m_vbuf.get(), static_cast<quint32>(first * sizeof(RenderVertex)),
                                      static_cast<quint32>(count * sizeof(RenderVertex)), &m_mesh.render()[first]);
            }
            m_pending.clear();
        }

        const QSize px = renderTarget()->pixelSize();
        QMatrix4x4 view = viewMatrix();
        QMatrix4x4 proj;
        proj.perspective(40.0f, static_cast<float>(px.width()) / std::max(1, px.height()), 2.0f, 600.0f);
        const QMatrix4x4 mvp = rhi()->clipSpaceCorrMatrix() * proj * view;
        const QVector3D light = view.inverted().mapVector(QVector3D(0.35f, 0.55f, 1.0f)).normalized();
        float block[20];
        std::copy_n(mvp.constData(), 16, block);
        block[16] = light.x();
        block[17] = light.y();
        block[18] = light.z();
        block[19] = 0;
        u->updateDynamicBuffer(m_ubuf.get(), 0, sizeof(block), block);

        cb->beginPass(renderTarget(), QColor(24, 24, 28), {1.0f, 0}, u);
        cb->setGraphicsPipeline(m_pipeline.get());
        cb->setViewport({0, 0, static_cast<float>(px.width()), static_cast<float>(px.height())});
        cb->setShaderResources(m_srb.get());
        const QRhiCommandBuffer::VertexInput input(m_vbuf.get(), 0);
        cb->setVertexInput(0, 1, &input, m_ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(m_indexCount);
        cb->endPass();

        const auto now = Clock::now();
        if (m_lastFrame.time_since_epoch().count() != 0) m_frameMs = msBetween(m_lastFrame, now);
        m_lastFrame = now;
    }

    void releaseResources() override {
        m_pipeline.reset();
        m_srb.reset();
        m_ubuf.reset();
        m_ibuf.reset();
        m_vbuf.reset();
    }

    void mousePressEvent(QMouseEvent* e) override {
        m_last = e->position();
        m_sculpting = e->button() == Qt::LeftButton;
        if (m_sculpting) sculpt(e);
    }
    void mouseReleaseEvent(QMouseEvent*) override { m_sculpting = false; }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_sculpting) {
            sculpt(e);
        } else if (e->buttons() & (Qt::RightButton | Qt::MiddleButton)) {
            m_yaw += static_cast<float>(e->position().x() - m_last.x()) * 0.4f;
            m_pitch = std::clamp(m_pitch + static_cast<float>(e->position().y() - m_last.y()) * 0.4f, -89.0f, 89.0f);
            update();
        }
        m_last = e->position();
    }
    void wheelEvent(QWheelEvent* e) override {
        m_dist = std::clamp(m_dist * std::pow(0.9f, static_cast<float>(e->angleDelta().y()) / 120.0f), 45.0f, 400.0f);
        update();
    }

private:
    QMatrix4x4 viewMatrix() const {
        QMatrix4x4 v;
        v.translate(0, 0, -m_dist);
        v.rotate(m_pitch, 1, 0, 0);
        v.rotate(m_yaw, 0, 1, 0);
        return v;
    }

    void sculpt(QMouseEvent* e) {
        const QSize s = size();
        QMatrix4x4 proj;
        proj.perspective(40.0f, static_cast<float>(s.width()) / std::max(1, s.height()), 2.0f, 600.0f);
        const QMatrix4x4 inv = (proj * viewMatrix()).inverted();
        const float x = 2.0f * static_cast<float>(e->position().x()) / s.width() - 1.0f;
        const float y = 1.0f - 2.0f * static_cast<float>(e->position().y()) / s.height();
        const QVector3D a = inv.map(QVector3D(x, y, -1.0f)), b = inv.map(QVector3D(x, y, 1.0f));
        const QVector3D d = (b - a).normalized();
        Vec3 hit;
        if (!m_mesh.raycast({a.x(), a.y(), a.z()}, {d.x(), d.y(), d.z()}, &hit)) return;
        const auto t0 = Clock::now();
        const bool dig = e->modifiers() & Qt::ShiftModifier;
        const EditResult r = edit(hit, m_brushRadius, dig ? -0.15f : 0.15f);
        setWindowTitle(QString("%1 %2 | edit %3 ms | upload %4 KB | frame %5 ms")
                           .arg(m_backend, m_device)
                           .arg(msBetween(t0, Clock::now()), 0, 'f', 1)
                           .arg(static_cast<double>(r.dirtyBytes) / 1024.0, 0, 'f', 0)
                           .arg(m_frameMs, 0, 'f', 1));
        update();
    }

    ClusterMesh& m_mesh;
    QRhi* m_rhi = nullptr;
    QString m_backend, m_device;
    std::unique_ptr<QRhiBuffer> m_vbuf, m_ibuf, m_ubuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    std::vector<uint32_t> m_indices;
    quint32 m_indexCount = 0;
    bool m_needFullUpload = true;
    std::vector<std::pair<size_t, size_t>> m_pending;
    EditResult m_lastEdit;

    float m_yaw = 20, m_pitch = 15, m_dist = 130, m_brushRadius = 3.0f;
    QPointF m_last;
    bool m_sculpting = false;
    Clock::time_point m_lastFrame{};
    double m_frameMs = 0;
};

int runBench(Viewport& view, ClusterMesh& mesh, int edits) {
    // The first grab creates the GPU resources and uploads the whole mesh.
    const auto w0 = Clock::now();
    view.grabFramebuffer();
    const double firstMs = msBetween(w0, Clock::now());
    view.grabFramebuffer();

    std::vector<double> base;
    for (int i = 0; i < 40; ++i) {
        const auto t = Clock::now();
        view.grabFramebuffer();
        base.push_back(msBetween(t, Clock::now()));
    }

    std::mt19937 rng(7);
    std::uniform_int_distribution<size_t> pickVertex(0, mesh.vertexCount() - 1);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    auto pick = [&] {
        for (;;) {  // even over the surface, not over the vertices
            const Vec3& p = mesh.positions()[pickVertex(rng)];
            const float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (unit(rng) < std::sqrt(std::max(0.0f, 1.0f - (p.y / r) * (p.y / r)))) return p;
        }
    };

    std::printf("Backend: %s, device: %s\n", view.backendName().toUtf8().constData(), view.deviceName().toUtf8().constData());
    std::printf("Mesh: %zu vertices, %zu triangles. Viewport %dx%d.\n", mesh.vertexCount(), mesh.triangleCount(), view.width(),
                view.height());
    std::printf("First frame including full upload of %.0f MB: %.0f ms.\n",
                static_cast<double>(mesh.render().size() * sizeof(RenderVertex)) / 1048576.0, firstMs);
    std::printf("Draw and read back one frame, no edit: p50 %.1f ms, p95 %.1f ms.\n\n", percentile(base, 0.5), percentile(base, 0.95));
    std::printf("| Brush | Radius (mm) | Upload (KB) | CPU update p50 (ms) | Edit to GPU done p50 (ms) | p95 (ms) | max (ms) |\n");
    std::printf("| --- | --- | --- | --- | --- | --- | --- |\n");

    const struct {
        const char* name;
        float radius;
    } brushes[] = {{"fine", 0.5f}, {"medium", 2.0f}, {"broad", 8.0f}};
    bool pass = true;
    for (const auto& b : brushes) {
        std::vector<double> cpu, total;
        double kb = 0;
        for (int i = 0; i < edits; ++i) {
            const Vec3 c = pick();
            const auto t0 = Clock::now();
            const EditResult r = view.edit(c, b.radius, i % 2 ? 0.3f : -0.3f);
            const auto t1 = Clock::now();
            view.grabFramebuffer();  // draws, uploads the changed ranges and waits for the GPU
            const auto t2 = Clock::now();
            cpu.push_back(msBetween(t0, t1));
            total.push_back(msBetween(t0, t2));
            kb += static_cast<double>(r.dirtyBytes) / 1024.0;
        }
        if (percentile(total, 0.95) > 50.0) pass = false;
        std::printf("| %s | %.1f | %.0f | %.2f | %.1f | %.1f | %.1f |\n", b.name, b.radius, kb / edits, percentile(cpu, 0.5),
                    percentile(total, 0.5), percentile(total, 0.95), *std::max_element(total.begin(), total.end()));
    }
    std::printf("\nTarget: edit to GPU done p95 <= 50 ms. %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    size_t vertices = 1000000;
    int edits = 100;
    bool bench = false;
    QString api, shot;
    int width = 1280, height = 720;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        const bool more = i + 1 < argc;
        if (a == "--bench") bench = true;
        else if (a == "--vertices" && more) vertices = QString::fromLocal8Bit(argv[++i]).toULongLong();
        else if (a == "--edits" && more) edits = QString::fromLocal8Bit(argv[++i]).toInt();
        else if (a == "--shot" && more) shot = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--api" && more) api = QString::fromLocal8Bit(argv[++i]).toLower();
        else if (a == "--size" && more) {
            const QStringList wh = QString::fromLocal8Bit(argv[++i]).split('x');
            if (wh.size() == 2) {
                width = wh[0].toInt();
                height = wh[1].toInt();
            }
        }
    }

    QApplication app(argc, argv);
    std::fprintf(stderr, "Building a %zu vertex mesh...\n", vertices);
    ClusterMesh mesh = ClusterMesh::makeSculpt(vertices, 1);

    Viewport view(mesh);
    // The default is Direct3D 11 on Windows, Metal on macOS and OpenGL elsewhere.
    // The conda-forge Qt used for releases has no Vulkan support.
    if (api == "opengl") view.setApi(QRhiWidget::Api::OpenGL);
    else if (!api.isEmpty()) std::fprintf(stderr, "Only --api opengl can be forced; using the default.\n");
    view.resize(width, height);
    view.setWindowTitle("Meshify P4 viewer");
    view.show();
    if (!shot.isEmpty()) {  // a picture after a few big edits, to check the drawing
        view.grabFramebuffer();
        for (int i = 0; i < 6; ++i) view.edit(mesh.positions()[(mesh.vertexCount() / 7) * (i + 1) % mesh.vertexCount()], 6.0f, i % 2 ? -3.0f : 3.0f);
        const QImage partial = view.grabFramebuffer();
        partial.save(shot);
        // The picture drawn from partial uploads must equal one drawn after uploading everything.
        view.forceFullUpload();
        const bool same = view.grabFramebuffer() == partial;
        std::printf("Partial uploads draw the same picture as a full upload: %s\n", same ? "yes" : "NO");
        if (!same) return 1;
    }
    if (bench) return runBench(view, mesh, edits);
    return app.exec();
}
