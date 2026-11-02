#include "motorglwidget.h"

#include <GL/gl.h>

#include <QtMath>

namespace {

void setColorRgb(float r, float g, float b, float emissive = 0.f)
{
    glColor3f(r + emissive, g + emissive, b + emissive);
}

void partColor(const char *part, const QString &runState, const QString &faultSite)
{
    const auto match = [&](const char *name) { return faultSite == QLatin1String(name); };

    const bool highlighted = match(part);

    if (runState == QLatin1String("normal")) {
        setColorRgb(0.39f, 0.42f, 0.45f);
        return;
    }
    if (!highlighted) {
        setColorRgb(0.39f, 0.42f, 0.45f);
        return;
    }
    if (runState == QLatin1String("notice")) {
        setColorRgb(0.15f, 0.39f, 0.77f, 0.06f);
    } else if (runState == QLatin1String("warning")) {
        setColorRgb(0.79f, 0.64f, 0.15f, 0.08f);
    } else if (runState == QLatin1String("alarm")) {
        setColorRgb(0.85f, 0.47f, 0.02f, 0.10f);
    } else if (runState == QLatin1String("emergency")) {
        setColorRgb(0.60f, 0.11f, 0.11f, 0.12f);
    } else {
        setColorRgb(0.73f, 0.11f, 0.11f, 0.10f);
    }
}

void drawBox(float sx, float sy, float sz)
{
    const float x = sx * 0.5f;
    const float y = sy * 0.5f;
    const float z = sz * 0.5f;
    glBegin(GL_QUADS);
    glVertex3f(-x, -y, z);
    glVertex3f(x, -y, z);
    glVertex3f(x, y, z);
    glVertex3f(-x, y, z);
    glVertex3f(-x, -y, -z);
    glVertex3f(-x, y, -z);
    glVertex3f(x, y, -z);
    glVertex3f(x, -y, -z);
    glVertex3f(-x, y, -z);
    glVertex3f(-x, y, z);
    glVertex3f(x, y, z);
    glVertex3f(x, y, -z);
    glVertex3f(-x, -y, -z);
    glVertex3f(x, -y, -z);
    glVertex3f(x, -y, z);
    glVertex3f(-x, -y, z);
    glVertex3f(x, -y, -z);
    glVertex3f(x, y, -z);
    glVertex3f(x, y, z);
    glVertex3f(x, -y, z);
    glVertex3f(-x, -y, -z);
    glVertex3f(-x, -y, z);
    glVertex3f(-x, y, z);
    glVertex3f(-x, y, -z);
    glEnd();
}

void drawPart(const char *name, float px, float py, float pz, float sx, float sy, float sz,
              const QString &runState, const QString &faultSite)
{
    partColor(name, runState, faultSite);
    glPushMatrix();
    glTranslatef(px, py, pz);
    drawBox(sx, sy, sz);
    glPopMatrix();
}

} // namespace

MotorGlWidget::MotorGlWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(480, 360);
    connect(&spinTimer_, &QTimer::timeout, this, [this]() {
        spin_ += 0.4f;
        if (spin_ >= 360.f)
            spin_ -= 360.f;
        update();
    });
    spinTimer_.start(16);
}

void MotorGlWidget::setTwinVisual(int deviceId, const QString &runState, const QString &faultSite)
{
    deviceId_ = deviceId;
    runState_ = runState;
    faultSite_ = faultSite;
    update();
}

void MotorGlWidget::initializeGL()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    const GLfloat lightPos[] = {4.f, 8.f, 6.f, 1.f};
    const GLfloat lightDiff[] = {1.f, 1.f, 1.f, 1.f};
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiff);
}

void MotorGlWidget::resizeGL(int w, int h)
{
    if (h <= 0)
        h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    glFrustum(-aspect * 0.35f, aspect * 0.35f, -0.35f, 0.35f, 0.5f, 40.f);
    glMatrixMode(GL_MODELVIEW);
}

void MotorGlWidget::paintGL()
{
    glClearColor(0.04f, 0.06f, 0.10f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.f, -0.3f, -5.f);
    glRotatef(18.f, 1.f, 0.f, 0.f);
    glRotatef(spin_, 0.f, 1.f, 0.f);
    glTranslatef(static_cast<float>((deviceId_ - 2) * 0.15f), 0.f, 0.f);

    drawPart("looseness", 0.f, 0.05f, 0.f, 2.4f, 0.12f, 1.3f, runState_, faultSite_);
    drawPart("body", 0.f, 0.85f, 0.f, 1.0f, 1.5f, 1.0f, runState_, faultSite_);
    drawPart("bearing", 0.f, 0.85f, -0.85f, 1.15f, 0.25f, 1.15f, runState_, faultSite_);
    drawPart("coupling", 0.f, 0.85f, 0.85f, 0.65f, 0.45f, 0.65f, runState_, faultSite_);
    drawPart("imbalance", 0.f, 0.85f, -1.05f, 1.3f, 0.12f, 1.3f, runState_, faultSite_);

    glDisable(GL_LIGHTING);
    glColor3f(0.15f, 0.22f, 0.32f);
    glBegin(GL_LINES);
    for (int i = -6; i <= 6; ++i) {
        glVertex3f(static_cast<float>(i), 0.f, -6.f);
        glVertex3f(static_cast<float>(i), 0.f, 6.f);
        glVertex3f(-6.f, 0.f, static_cast<float>(i));
        glVertex3f(6.f, 0.f, static_cast<float>(i));
    }
    glEnd();
    glEnable(GL_LIGHTING);
}
