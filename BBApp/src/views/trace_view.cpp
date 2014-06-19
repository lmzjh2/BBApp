#include "trace_view.h"
#include "model/session.h"
#include "model/trace.h"
#include "lib/bb_lib.h"
#include "mainwindow.h"

#include <QToolTip>
#include <QMouseEvent>
#include <QPushButton>

#include <vector>

#define PERSIST_WIDTH 1280
#define PERSIST_HEIGHT 720

#define M_PI 3.14159265

#pragma warning(disable:4305)
#pragma warning(disable:4267)

static void normalize(float* f)
{
    float invMag , mag;
    mag = sqrt(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if(mag == 0) mag = 0.1e-5f;
    invMag = (float)1.0 / mag;
    f[0] *= invMag;
    f[1] *= invMag;
    f[2] *= invMag;
}

static void cross(float* r , float* a , float* b)
{
    r[0] = a[1]*b[2] - a[2]*b[1];
    r[1] = a[2]*b[0] - a[0]*b[2];
    r[2] = a[0]*b[1] - a[1]*b[0];
}

static inline float det(float a, float b, float c, float d)
{
    return a*d - b*c;
}

static inline void swap(float* f1, float* f2)
{
    float temp = *f1;
    *f1 = *f2;
    *f2 = temp;
}

void glPerspective(float angle, float aRatio, float near_val, float far_val)
{
    float R = tan((angle * (M_PI / 360.0f))) * near_val;
    float T = R * aRatio;

    glFrustum(-T, T, -R, R, near_val, far_val);
}

void glLookAt(float ex, float ey, float ez,
              float cx, float cy, float cz,
              float ux, float uy, float uz)
{
    float f[3];    // Forward Vector
    float UP[3];   // Up Vector
    float s[3];    // Side Vector
    float u[3];	   // Recomputed up
    float LA[16];  // Look At Matrix

    // Find Forward Vector
    f[0] = cx - ex;
    f[1] = cy - ey;
    f[2] = cz - ez ;
    normalize(f);
    // Normalize Up
    UP[0] = ux;
    UP[1] = uy;
    UP[2] = uz;
    normalize(UP);
    // Find s and u
    cross(s, f, UP);
    normalize(s);
    cross(u, s, f);

    // Build Look At Matrix
    LA[0] = s[0]; LA[1] = u[0]; LA[2] = -f[0]; LA[3] = 0;
    LA[4] = s[1]; LA[5] = u[1]; LA[6] = -f[1]; LA[7] = 0;
    LA[8] = s[2]; LA[9] = u[2]; LA[10] = -f[2]; LA[11] = 0;
    LA[12] = 0; LA[13] = 0; LA[14] = 0; LA[15] = 1;

    glMultMatrixf(LA);
    glTranslatef(-ex, -ey, -ez);
}

static void sphereToCart(float theta, float phi, float rho,
                         float *x, float *y, float *z)
{
    *x = rho*sin(phi)*cos(theta);
    *y = rho*sin(phi)*sin(theta);
    *z = rho*cos(phi);
}

static float rho = 1, theta = -0.5 * M_PI, phi = 0.4 * M_PI;
static int mx, my;
static bool dragging = false;
static const float RPP = 0.01;

TraceView::TraceView(Session *session, QWidget *parent)
    : QGLWidget(parent),
      session_ptr(session),
      persist_on(false),
      clear_persistence(false),
      waterfall_state(WaterfallOFF),
      textFont("Arial", 14),
      divFont("Arial", 12),
      hasOpenGL3(false)
{
    setAutoBufferSwap(false);
    setMouseTracking(true);

    time.start();

    for(int i = 0; i < 11; i++) {
        graticule.push_back(0.0);
        graticule.push_back(0.1 * i);
        graticule.push_back(1.0);
        graticule.push_back(0.1 * i);
    }

    for(int i = 0; i < 11; i++) {
        graticule.push_back(0.1 * i);
        graticule.push_back(0.0);
        graticule.push_back(0.1 * i);
        graticule.push_back(1.0);
    }

    grat_border.push_back(0.0);
    grat_border.push_back(0.0);
    grat_border.push_back(1.0);
    grat_border.push_back(0.0);
    grat_border.push_back(1.0);
    grat_border.push_back(1.0);
    grat_border.push_back(0.0);
    grat_border.push_back(1.0);
    grat_border.push_back(0.0);
    grat_border.push_back(0.0);

    makeCurrent();
    initializeOpenGLFunctions();

    glShadeModel(GL_SMOOTH);
    glClearDepth(1.0);
    glDepthFunc(GL_LEQUAL);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    context()->format().setDoubleBuffer(true);

    glGenBuffers(1, &traceVBO);
    glGenBuffers(1, &textureVBO);
    glGenBuffers(1, &gratVBO);
    glGenBuffers(1, &borderVBO);

    glBindBuffer(GL_ARRAY_BUFFER, gratVBO);
    glBufferData(GL_ARRAY_BUFFER, graticule.size()*sizeof(float),
                 &graticule[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, borderVBO);
    glBufferData(GL_ARRAY_BUFFER, grat_border.size()*sizeof(float),
                 &grat_border[0], GL_STATIC_DRAW);

    // Setup persistence if openGL version 3 available
    const unsigned char *version = glGetString(GL_VERSION);
    if(version) {
        char v = version[0];
        if(atoi(&v) >= 3) {
            persist_program = std::unique_ptr<GLProgram>(
                        new GLProgram(persist_vs, persist_fs));
            persist_program->Compile(this);

            InitPersistFBO();
            hasOpenGL3 = true;
        }
    }

    waterfall_tex = get_texture_from_file(":/color_spectrogram.png");

    doneCurrent();

    swap_thread = new SwapThread(this);
    swap_thread->start();
    Sleep(25);
}

TraceView::~TraceView()
{
    swap_thread->Stop();
    paintCondition.notify();
    swap_thread->wait();

    makeCurrent();
    glDeleteBuffers(1, &traceVBO);
    glDeleteBuffers(1, &textureVBO);
    glDeleteBuffers(1, &gratVBO);
    glDeleteBuffers(1, &borderVBO);
    doneCurrent();

    delete swap_thread;
    ClearWaterfall();

    Sleep(100);
}

bool TraceView::InitPersistFBO()
{
    bool complete = false;

    // Render buffer start, build the tex we are writing to
    glGenTextures(1, &persist_tex);
    glBindTexture(GL_TEXTURE_2D, persist_tex);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE); // automatic mipmap
    glTexImage2D(GL_TEXTURE_2D,	0, GL_RGBA,	PERSIST_WIDTH, PERSIST_HEIGHT,
                 0, GL_RGBA,	GL_UNSIGNED_BYTE, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Generate our FBO depth buffer
    glGenRenderbuffers(1, &persist_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, persist_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
                          PERSIST_WIDTH, PERSIST_HEIGHT);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &persist_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, persist_fbo);

    // attach the texture to FBO color attachment point
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, persist_tex, 0);

    // attach the renderbuffer to depth attachment point
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, persist_depth);

    // check FBO status
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE) {
        //pDoc->PutLog("FBO Not Complete\n");
        complete = false;
    } else {
        complete = true;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return complete;
}

void TraceView::resizeEvent(QResizeEvent *)
{
    grat_ll = QPoint(60, 50);
    grat_ul = QPoint(60, size().height() - 50);

    grat_sz = QPoint(size().width() - 80,
                     size().height() - 100);

    //QGLWidget::resize(size());
}

/*
 * In future, if currently drawing, queue up one draw? if possible
 */
void TraceView::paintEvent(QPaintEvent *)
{
    // Draw if possible, otherwise nothing?
    if(drawMutex.try_lock()) {
        paintCondition.notify();
        Paint();
        context()->moveToThread(swap_thread);
        drawMutex.unlock();
    }
}

// Place marker in simple case
void TraceView::mousePressEvent(QMouseEvent *e)
{
    if(PointInGrat(e->pos())) {
        // Make point relative to upper left of graticule
        int x_pos = e->pos().x() - grat_ul.x();

        if(x_pos < 0 || x_pos > grat_sz.x())
            return;

        session_ptr->trace_manager->PlaceMarker((double)x_pos / grat_sz.x());

    } else if (waterfall_state == Waterfall3D) {
        dragging = true;
        mx = e->pos().x();
        my = e->pos().y();
    }

    QGLWidget::mousePressEvent(e);
}

void TraceView::mouseReleaseEvent(QMouseEvent *)
{
    dragging = false;
}

void TraceView::mouseMoveEvent(QMouseEvent *e)
{   
    if(PointInGrat(e->pos())) {
        const SweepSettings *s = session_ptr->sweep_settings;
        double x, xScale, y, yScale;

        xScale = s->Span() / grat_sz.x();
        x = s->Start() + xScale * (e->pos().x() - grat_ll.x());

        if(s->RefLevel().IsLogScale()) {
            yScale = (s->Div() * 10.0) / grat_sz.y();
        } else {
            yScale = s->RefLevel() / grat_sz.y();
        }

        y = s->RefLevel() - (e->pos().y() - grat_ll.y()) * yScale;
        MainWindow::GetStatusBar()->SetCursorPos(
                    Frequency(x).GetFreqString() + "  " +
                    Amplitude(y, s->RefLevel().Units()).GetString());

    } else {
        MainWindow::GetStatusBar()->SetCursorPos("");
    }

    if(dragging) {
        // This math affects our spherical coords for viewing 3D waterfall
        int dx = e->pos().x() - mx, dy = e->pos().y() - my;
        theta -= dx * RPP;
        phi -= dy * RPP;
        if(phi < 0.1 * M_PI) phi = 0.1 * M_PI;
        if(phi > 0.5 * M_PI) phi = 0.5 * M_PI;
        if(theta < -0.75 * M_PI) theta = -0.75 * M_PI;
        if(theta > -0.25 * M_PI) theta = -0.25 * M_PI;
        mx = e->pos().x();
        my = e->pos().y();
        update();
    }

    QGLWidget::mouseMoveEvent(e);
}

void TraceView::wheelEvent(QWheelEvent *e)
{
    if(e->delta() < 0) rho += 0.1;
    if(e->delta() > 0) rho -= 0.1;
    if(rho < 0.5) rho = 0.5;
    if(rho > 4.0) rho = 4.0;
    update();

    QGLWidget::wheelEvent(e);
}

/*
 * Main Paint Routine
 */
void TraceView::Paint()
{
    // Before we paint, ensure the glcontext belongs to the main thread
    if(context()->contextHandle()->thread() != QApplication::instance()->thread()) {
        return;
    }

    makeCurrent();

    glQClearColor(session_ptr->colors.background, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // Must be called for textures to be drawn properly
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);

    // Draw nothing but background color if the graticule has a
    //   negative width or height
    if(grat_sz.x() <= 0 || grat_sz.y() <= 0) {
        return;
    }

    // Calculate dimensions based on window size
    grat_ll = QPoint(60, 50);
    QPoint grat_upper_left = QPoint(60, size().height() - 50);
    QPoint grat_size = QPoint(width() - 80, height() - 100);

    if(!session_ptr->GetTitle().isNull()) {
        grat_upper_left = QPoint(60, size().height() - 70);
        grat_size = QPoint(width() - 80, height() - 120);
    }

    if(waterfall_state != WaterfallOFF) {
        grat_upper_left = QPoint(60, size().height() / 2);
        grat_size = QPoint(width() - 80, size().height() / 2 - 50);
    }
    grat_ul = grat_upper_left;
    grat_sz = grat_size;

    glViewport(0, 0, size().width(), size().height());
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, size().width(), 0, size().height(), -1, 1);

    glEnable(GL_DEPTH_TEST);
    glEnableClientState(GL_VERTEX_ARRAY);

    RenderGraticule(); // Graticule always shown

    if(!session_ptr->device->IsOpen()) {
        glQColor(session_ptr->colors.text);
        DrawString(tr("No Device Connected"), QFont("Arial", 14),
                   QPoint(grat_ul.x(), grat_ul.y() + 5), LEFT_ALIGNED);

    } else if(session_ptr->sweep_settings->Mode() == BB_IDLE) {
        glQColor(session_ptr->colors.text);
        DrawString(tr("Device Idle"), QFont("Arial", 14),
                   QPoint(grat_ul.x(), grat_ul.y() + 5), LEFT_ALIGNED);
    } else {

        RenderTraces();
        if(waterfall_state != WaterfallOFF) {
            DrawWaterfall();
        }

        glDisable(GL_DEPTH_TEST);
        glDisableClientState(GL_VERTEX_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        RenderGratText();
        RenderMarkers();
        RenderChannelPower();

    }

    doneCurrent();
}

void TraceView::RenderGraticule()
{
    // Model view for graticule
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glTranslatef(grat_ll.x(), grat_ll.y(), 0);
    glScalef(grat_sz.x(), grat_sz.y(), 1.0);

    // Projection for graticule
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, size().width(), 0, size().height(), -1, 1);

    glLineWidth(session_ptr->prefs.graticule_width);
    glQColor(session_ptr->colors.graticule);

    // Draw inner grat
    if(session_ptr->prefs.graticule_stipple) {
        glLineStipple(1, 0x8888);
        glEnable(GL_LINE_STIPPLE);
    }

    glBindBuffer(GL_ARRAY_BUFFER, gratVBO);
    glVertexPointer(2, GL_FLOAT, 0, OFFSET(0));
    glDrawArrays(GL_LINES, 0, graticule.size()/2);

    if(session_ptr->prefs.graticule_stipple) {
        glDisable(GL_LINE_STIPPLE);
    }

    // Border
    glBindBuffer(GL_ARRAY_BUFFER, borderVBO);
    glVertexPointer(2, GL_FLOAT, 0, OFFSET(0));
    glDrawArrays(GL_LINE_STRIP, 0, grat_border.size()/2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glLineWidth(1.0);
}

void TraceView::RenderGratText()
{
    glQColor(session_ptr->colors.text);

    //glViewport(0, 0, size().width(), size().height());

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, size().width(), 0, size().height(), -1, 1);

    const SweepSettings *s = session_ptr->sweep_settings;
    TraceManager *tm = session_ptr->trace_manager;
    QVariant elapsed = time.restart();
    Frequency freq_off = tm->FreqOffset();
    QString str;

    double div = s->RefLevel().IsLogScale() ? s->Div() : (s->RefLevel().Val() / 10.0);

    str = session_ptr->GetTitle();
    if(!str.isNull()) {
        DrawString(str, QFont("Arial", 20), width() / 2, height() - 22, CENTER_ALIGNED);
    }

    str.sprintf("Elapsed %d, SweepSize %d", elapsed.toInt(),
                tm->GetTrace(0)->Length());
    DrawString(str, textFont, grat_ll.x()+grat_sz.x()-5,
               grat_ll.y()-40, RIGHT_ALIGNED);
    DrawString("Center " + (s->Center() + freq_off).GetFreqString(), textFont,
               size().width()/2, grat_ll.y()-20, CENTER_ALIGNED);
    DrawString("Span " + s->Span().GetFreqString(), textFont,
               size().width()/2, grat_ll.y()-40, CENTER_ALIGNED);
    DrawString("Start " + (s->Start() + freq_off).GetFreqString(), textFont,
               grat_ll.x()+5, grat_ll.y()-20, LEFT_ALIGNED);
    DrawString("Stop " + (s->Stop() + freq_off).GetFreqString(), textFont,
               grat_ll.x()+grat_sz.x()-5, grat_ll.y()-20, RIGHT_ALIGNED);
    DrawString("Ref " + s->RefLevel().GetString(), textFont,
               grat_ll.x()+5, grat_ul.y()+22, LEFT_ALIGNED);
    str.sprintf("Div %.1f", div);
    DrawString(str, textFont, grat_ul.x()+5, grat_ul.y()+2, LEFT_ALIGNED);
    DrawString("RBW " + s->RBW().GetFreqString(), textFont,
               size().width()/2, grat_ul.y()+22, CENTER_ALIGNED);
    s->GetAttenString(str);
    DrawString(str, textFont, size().width() / 2, grat_ul.y() + 2, CENTER_ALIGNED);
    DrawString("VBW " + s->VBW().GetFreqString(), textFont,
               grat_ul.x()+grat_sz.x()-5, grat_ul.y()+22, RIGHT_ALIGNED);

    // y-axis labels
    for(int i = 0; i <= 8; i += 2) {
        int x_pos = 58, y_pos = (grat_sz.y() / 10) * i + grat_ll.y() - 5;
        QString div_str;
        div_str.sprintf("%.2f", s->RefLevel() - (div*(10-i)));
        DrawString(div_str, divFont, x_pos, y_pos, RIGHT_ALIGNED);
    }

    if(tm->GetLimitLine()->Active()) {
        QPoint limitTextLoc(grat_ul.x() + (grat_sz.x() * 0.5),
                            grat_ul.y() - (grat_sz.y() * 0.25));
        if(tm->GetLimitLine()->LimitsPassed()) {
            glColor3f(0.0, 1.0, 0.0);
            DrawString("Passed", textFont, limitTextLoc, CENTER_ALIGNED);
        } else {
            glColor3f(1.0, 0.0, 0.0);
            DrawString("Failed", textFont, limitTextLoc, CENTER_ALIGNED);
        }
    }

    // Amplitude high warning
    if(session_ptr->trace_manager->LastTraceAboveReference()) {
        glColor3f(1.0, 0.0, 0.0);
        DrawString("*Warning* : Signal Level Higher Than Reference Level", textFont,
                   (grat_ul.x() + grat_sz.x()) / 2.0, grat_ul.y() - 22, CENTER_ALIGNED);
    }

    // Uncal text strings
    bool uncal = false;
    int uncal_x = grat_ul.x() + 5, uncal_y = grat_ul.y() - 22;
    glColor3f(1.0, 0.0, 0.0);
    if(!session_ptr->device->IsPowered()) {
        uncal = true;
        DrawString("Low Voltage", textFont, uncal_x, uncal_y, LEFT_ALIGNED);
        uncal_y -= 16;
    }
    if(session_ptr->device->ADCOverflow()) {
        uncal = true;
        DrawString("IF Overload", textFont, uncal_x, uncal_y, LEFT_ALIGNED);
        uncal_y -= 16;
    }
    if(session_ptr->device->NeedsTempCal()) {
        uncal = true;
        DrawString("Device Temp", textFont, uncal_x, uncal_y, LEFT_ALIGNED);
        uncal_y -= 16;
    }
    if(uncal) {
        DrawString("Uncal", textFont, grat_ul.x() - 5, grat_ul.y() - 22, RIGHT_ALIGNED);
    }

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

void TraceView::RenderTraces()
{
    TraceManager *manager = session_ptr->trace_manager;

    // Un-buffer persist/waterfall data
    if(persist_on || (waterfall_state != WaterfallOFF)) {
        GLVector *v_ptr = nullptr;
        while(v_ptr = manager->trace_buffer.Back()) {
            if(persist_on) {
                AddToPersistence(*v_ptr);
            }
            if(waterfall_state != WaterfallOFF) {
                AddToWaterfall(*v_ptr);
            }
            manager->trace_buffer.IncrementBack();
        }
    }

    if(persist_on) {
        DrawPersistence();
        return;
    }

    // Prep viewport
    glPushAttrib(GL_VIEWPORT_BIT);
    glViewport(grat_ll.x(), grat_ll.y(),
               grat_sz.x(), grat_sz.y());

    // Prep modelview
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Ortho
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);

    // Nice lines, doesn't smooth quads
    glEnable(GL_BLEND);
    glEnable(GL_LINE_SMOOTH);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glLineWidth(session_ptr->prefs.trace_width);

    manager->Lock();

    // Loop through each trace
    for(int i = 0; i < TRACE_COUNT; i++) {
        // If Trace is active, normalize and draw it
        const Trace *trace = manager->GetTrace(i);

        if(trace->Active()) {
            normalize_trace(trace, traces[i], grat_sz);
            DrawTrace(trace, traces[i]);
        }
    }

    manager->Unlock();

    if(manager->GetLimitLine()->Active()) {
        normalize_trace(&manager->GetLimitLine()->store, traces[0], grat_sz);
        DrawLimitLines(&manager->GetLimitLine()->store, traces[0]);
    }

    // Disable nice lines
    glLineWidth(1.0);
    glDisable(GL_BLEND);
    glDisable(GL_LINE_SMOOTH);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glPopAttrib();
}

void TraceView::DrawTrace(const Trace *t, const GLVector &v)
{
    if(v.size() < 1) {
        return;
    }

    QColor c = t->Color();
    glColor3f(c.redF(), c.greenF(), c.blueF());

    // Put the trace in the vbo
    glBindBuffer(GL_ARRAY_BUFFER, traceVBO);
    glBufferData(GL_ARRAY_BUFFER, v.size()*sizeof(float),
                 &v[0], GL_DYNAMIC_DRAW);
    glVertexPointer(2, GL_FLOAT, 0, OFFSET(0));

    // Draw fill
    glDrawArrays(GL_TRIANGLE_STRIP, 0, v.size() / 2);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // Draw lines
    glVertexPointer(2, GL_FLOAT, 16, OFFSET(0));
    glDrawArrays(GL_LINE_STRIP, 0, v.size() / 4);
    glVertexPointer(2, GL_FLOAT, 16, OFFSET(8));
    glDrawArrays(GL_LINE_STRIP, 0, v.size() / 4);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Unbind array
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/*
 * RenderMarkers()
 */
void TraceView::RenderMarkers()
{
    SweepSettings *s = session_ptr->sweep_settings;
    TraceManager *tm = session_ptr->trace_manager;

    // Viewport on grat, full pixel scale
    glPushAttrib(GL_VIEWPORT_BIT);
    glViewport(grat_ll.x(), grat_ll.y(), grat_sz.x(), grat_sz.y());

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, grat_sz.x(), 0, grat_sz.y(), -1, 1);

    int x_print = grat_sz.x() - 5;
    int y_print = grat_sz.y() - 20;

    tm->SolveMarkers(s);

    // Nice lines, doesn't smooth quads
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glLineWidth(1.0);

    for(int i = 0; i < MARKER_COUNT; i++) {
        Marker *m = tm->GetMarker(i);
        if(!m->Active() || !m->InView()) {
            continue;
        }

        if(m->InView()) {
            DrawMarker(m->xRatio() * grat_sz.x(),
                       m->yRatio() * grat_sz.y(), i + 1);
        }

        if(m->DeltaActive() && m->DeltaInView()) {
            DrawDeltaMarker(m->delxRatio() * grat_sz.x(),
                            m->delyRatio() * grat_sz.y(), i + 1);
        }
        // Does not have to be in view to draw the delta values
        if(m->DeltaActive()) {
            glQColor(session_ptr->colors.markerText);
            DrawString("Mkr " + QVariant(i+1).toString() + " Delta: " + m->DeltaText(),
                       textFont, QPoint(x_print, y_print), RIGHT_ALIGNED);
            y_print -= 20;
        } else if(m->Active()) {
            glQColor(session_ptr->colors.markerText);
            DrawString("Mkr " + QVariant(i+1).toString() + ": " + m->Text(),
                       textFont, QPoint(x_print, y_print), RIGHT_ALIGNED);
            y_print -= 20;
        }
    }

    // Disable nice lines
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}

/*
 * DrawMarker()
 */
void TraceView::DrawMarker(int x, int y, int num)
{
    glColor3f(1.0, 1.0, 1.0);
    glBegin(GL_POLYGON);
    glVertex2f(x, y);
    glVertex2f(x + 10, y + 15);
    glVertex2f(x, y + 30);
    glVertex2f(x - 10, y + 15);
    glEnd();

    //glQColor(session_ptr->colors.markers);
    glColor3f(0.0, 0.0, 0.0);
    glBegin(GL_LINE_STRIP);
    glVertex2f(x, y);
    glVertex2f(x + 10, y + 15);
    glVertex2f(x, y + 30);
    glVertex2f(x - 10, y + 15);
    glVertex2f(x, y);
    glEnd();

    glColor3f(0.0, 0.0, 0.0);
    QString str;
    str.sprintf("%d", num);
    DrawString(str, divFont,
               QPoint(x, y + 10), CENTER_ALIGNED);
}

void TraceView::DrawDeltaMarker(int x, int y, int num)
{
    glColor3f(1.0, 1.0, 1.0);
    glBegin(GL_POLYGON);
    glVertex2f(x, y);
    glVertex2f(x + 11, y + 11);
    glVertex2f(x + 11, y + 27);
    glVertex2f(x - 11, y + 27);
    glVertex2f(x - 11, y + 11);
    glEnd();

    //glQColor(session_ptr->colors.markers);
    glColor3f(0.0, 0.0, 0.0);
    glBegin(GL_LINE_STRIP);
    glVertex2f(x, y);
    glVertex2f(x + 11, y + 11);
    glVertex2f(x + 11, y + 27);
    glVertex2f(x - 11, y + 27);
    glVertex2f(x - 11, y + 11);
    glVertex2f(x, y);
    glEnd();

    glColor3f(0.0, 0.0, 0.0);
    QString str;
    str.sprintf("R%d", num);
    DrawString(str, divFont,
               QPoint(x, y+11), CENTER_ALIGNED);
}

void TraceView::DrawString(const QString &s,
                           const QFont &f,
                           QPoint p,
                           TextAlignment align)
{
    if(align == RIGHT_ALIGNED) {
        p -= QPoint(GetTextWidth(s, f), 0);
    } else if(align == CENTER_ALIGNED) {
        p -= QPoint(GetTextWidth(s, f)/2, 0);
    }

    renderText(p.x(), p.y(), 0, s, f);
}

void TraceView::RenderChannelPower()
{
    const ChannelPower *cp = session_ptr->trace_manager->GetChannelPowerInfo();
    if(!cp->IsEnabled()) return;

    double start = session_ptr->sweep_settings->Start();
    double stop = session_ptr->sweep_settings->Stop();
    double span = stop - start;
    if(span == 0.0) return;

    glPushAttrib(GL_VIEWPORT_BIT);
    glViewport(grat_ll.x(), grat_ll.y(), grat_sz.x(), grat_sz.y());
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    //glTranslatef(grat_ul.x(), grat_ul.y(), 0.0);
    //glScalef(grat_sz.x(), grat_sz.y(), 1.0);
    //glScalef(1.0, grat_sz.y(), 1.0);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, grat_sz.x(), 0, grat_sz.y(), -1, 1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for(int i = 0; i < 3; i++) {
        if(!cp->IsChannelInView(i)) continue;

        double x1 = (cp->GetChannelStart(i) - start) / span,
                x2 = (cp->GetChannelStop(i) - start) / span;
        double xCen = (x1 + x2) / 2.0;

        glColor4f(0.5, 0.5, 0.5, 0.4);
        glBegin(GL_QUADS);
        glVertex2f(x1 * grat_sz.x(), 0.0);
        glVertex2f(x2 * grat_sz.x(), 0.0);
        glVertex2f(x2 * grat_sz.x(), grat_sz.y());//1.0);
        glVertex2f(x1 * grat_sz.x(), grat_sz.y());//1.0);
        glEnd();

        // Draw Channel power text
        glQColor(session_ptr->colors.text);
        QString cp_string;
        cp_string.sprintf("%f", cp->GetChannelPower(i));
        DrawString(cp_string, textFont, xCen * grat_sz.x(), 40, CENTER_ALIGNED);

        // Draw dBc power text
        if(i == 0 || i == 2) {
            cp_string.sprintf("%f dBc", cp->GetChannelPower(i) - cp->GetChannelPower(1));
            DrawString(cp_string, textFont, xCen * grat_sz.x(), 20, CENTER_ALIGNED);
        }
    }

    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

void TraceView::DrawPersistence()
{
    // Draw a single quad over our grat
    glUseProgram(persist_program->Handle());
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, persist_tex);
    glGenerateMipmap(GL_TEXTURE_2D);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glTranslatef(grat_ll.x(), grat_ll.y(), 0.0);
    glScalef(grat_sz.x(), grat_sz.y(), 1.0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(0,0);
    glTexCoord2f(0,1); glVertex2f(0,1);
    glTexCoord2f(1,1); glVertex2f(1,1);
    glTexCoord2f(1,0); glVertex2f(1,0);
    glEnd();

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();

    glUseProgram(0);
}

void TraceView::DrawLimitLines(const Trace *limitTrace, const GLVector &v)
{
    if(limitTrace->Length() < 1) return;

    glLineWidth(3.0);
    glQColor(session_ptr->colors.limitLines);

    glBindBuffer(GL_ARRAY_BUFFER, traceVBO);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float),
                 &v[0], GL_DYNAMIC_DRAW);

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // Draw max
    glVertexPointer(2, GL_FLOAT, 4*sizeof(float), (GLvoid*)0);
    glDrawArrays(GL_LINE_STRIP, 0, v.size() / 4);
    // Draw min
    glVertexPointer(2, GL_FLOAT, 4*sizeof(float), (GLvoid*)(2*sizeof(float)));
    glDrawArrays(GL_LINE_STRIP, 0, v.size() / 4);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glPopMatrix();

    glLineWidth(1.0);


//    if(v.size() < 1) {
//        return;
//    }

//    QColor c = t->Color();
//    glColor3f(c.redF(), c.greenF(), c.blueF());

//    // Put the trace in the vbo
//    glBindBuffer(GL_ARRAY_BUFFER, traceVBO);
//    glBufferData(GL_ARRAY_BUFFER, v.size()*sizeof(float),
//                 &v[0], GL_DYNAMIC_DRAW);
//    glVertexPointer(2, GL_FLOAT, 0, OFFSET(0));

//    // Draw fill
//    glDrawArrays(GL_TRIANGLE_STRIP, 0, v.size() / 2);
//    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

//    // Draw lines
//    glVertexPointer(2, GL_FLOAT, 16, OFFSET(0));
//    glDrawArrays(GL_LINE_STRIP, 0, v.size() / 4);
//    glVertexPointer(2, GL_FLOAT, 16, OFFSET(8));
//    glDrawArrays(GL_LINE_STRIP, 0, v.size() / 4);
//    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

//    // Unbind array
//    glBindBuffer(GL_ARRAY_BUFFER, 0);

}

void TraceView::AddToPersistence(const GLVector &v)
{
    if(v.size() < 1) return;

    // Prep GL state, bind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, persist_fbo);

    if(clear_persistence) {
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        clear_persistence = false;
    }

    glViewport(0, 0, PERSIST_WIDTH, PERSIST_HEIGHT);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthFunc(GL_LESS); // Only pass less
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0);

    // Prep matrices
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glScalef(PERSIST_WIDTH, PERSIST_HEIGHT, 1.0);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, PERSIST_WIDTH, 0, PERSIST_HEIGHT, -1, 1);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);

    // Reduce current color by blending a big full screen quad
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0, 0.0, 0.0, /*persistDecay*/2 * .01);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(0,0);
    glTexCoord2f(0,1); glVertex2f(0,1);
    glTexCoord2f(1,1); glVertex2f(1,1);
    glTexCoord2f(1,0); glVertex2f(1,0);
    glEnd();

    // Prep the trace, use decay rate to add to persistence
    glBlendFunc(GL_ONE, GL_ONE);
    glBindBuffer(GL_ARRAY_BUFFER, traceVBO);
    glColor3f(0.04, 0.04, 0.04);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float),
                  &v[0], GL_DYNAMIC_DRAW);
    glVertexPointer(2, GL_FLOAT, 0, (GLvoid*)0);

    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Draw the trace
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDrawArrays(GL_QUAD_STRIP, 0, v.size() / 2);
    glTranslatef(0, 0, -0.5);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawArrays(GL_QUAD_STRIP, 0, v.size() / 2);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Revert GL state
    glDisable(GL_BLEND);
    glDepthFunc(GL_LEQUAL);
    glLineWidth(1.0);
    glViewport(0, 0, width(), height());
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    // Unbind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void TraceView::AddToWaterfall(const GLVector &v)
{
    bool degenHack = false; // prevents degenerate polygons
    float x, z;

    // Gonna have to remove 'n' falls based on the actual
    //      view height / pixPerFall
    if(v.size() <= 0) return;

    // Remove all buffers past max size
    while (waterfall_verts.size() >= MAX_WATERFALL_LINES) {
        delete waterfall_verts.back();
        waterfall_verts.pop_back();
        delete waterfall_coords.back();
        waterfall_coords.pop_back();
    }

    if(v.size() * 0.25 > grat_sz.x() * 0.5)
        degenHack = true;

    waterfall_verts.insert(waterfall_verts.begin(), new GLVector);
    waterfall_coords.insert(waterfall_coords.begin(), new GLVector);

    waterfall_verts[0]->reserve((v.size() * 3) >> 1); // Re-look at these values
    waterfall_coords[0]->reserve(v.size() * 2);

    // Center samples, if/else on degen hack, to draw poly's greater than 1 pixel wide
    for(unsigned i = 0; i < v.size(); i += 4) {

        if(degenHack) {
            // Get max for three points, one on each side of sample in question
            x = v[i];
            z = bb_lib::max3(v[bb_lib::max2(1, (int)i-1)], // Sample to left
                    v[i+1], v[bb_lib::min2(i+3, (unsigned)v.size()-3)]);
        } else {
            // x & z for every sample
            x = v[i];
            z = v[i+1];
        }

        // Best place to clamp. This clamps the tex coord and pos
        // Must clamp height in waterfall because we don't have
        //   the luxury of clipping with a viewport.
        bb_lib::clamp(z, 0.0f, 1.0f);

        // Max Point
        waterfall_verts[0]->push_back(x);
        waterfall_verts[0]->push_back(0.0);
        waterfall_verts[0]->push_back(z);

        // Min Point
        waterfall_verts[0]->push_back(x);
        waterfall_verts[0]->push_back(0.0);
        waterfall_verts[0]->push_back(0.0);

        // Set Tex Coords
        waterfall_coords[0]->push_back(x);
        waterfall_coords[0]->push_back(z);

        waterfall_coords[0]->push_back(x);
        waterfall_coords[0]->push_back(0.0);
    }
}

void TraceView::ClearWaterfall()
{
    while(!waterfall_verts.empty()) {
        delete waterfall_verts.back();
        waterfall_verts.pop_back();
    }

    while(!waterfall_coords.empty()) {
        delete waterfall_coords.back();
        waterfall_coords.pop_back();
    }
}

/*
 * Draws our waterfall buffers
 * Three steps for both 2&3 Dimensional drawing
 * For each step, common setup first, then if/else for mode dependent stuff
 * Step 1) Setup
 * Step 2) VBO buffering and Drawing
 * Step 3) Break-Down/Revert GL state
 */
void TraceView::DrawWaterfall()
{
    // Step 1 : Setup
    // Do Setup Based on waterfall mode
    //
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    //glEnable(GL_LINE_SMOOTH);
    //glEnable(GL_BLEND);

    glPushAttrib(GL_VIEWPORT_BIT);
    // Load the proper spectrum texture
    //if(this->colorBlindSpectrum)
    //    glBindTexture( GL_TEXTURE_2D, cbSpectrumTex );
    //else
        glBindTexture(GL_TEXTURE_2D, waterfall_tex);

    if(waterfall_state == Waterfall2D) {
        // Create perfect fit viewport for 2D waterfall, auto clips for us
        glViewport(grat_ul.x(), grat_ul.y() + 50, grat_sz.x(), height() * 0.40);
        glMatrixMode( GL_MODELVIEW );
        glPushMatrix();
        glLoadIdentity();
        glScalef((float)grat_sz.x(), 1.0f, 1.0f);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, grat_sz.x(), 0, height() * 0.35, -1, 1);

    } else if(waterfall_state == Waterfall3D) {
        glViewport(0, grat_ul.y() + 50, width(), height() * 0.4);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glPerspective(90, (float)(0.4 * width() / height()), 0.1, 100);
        // Look At stuff
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        float ex, ey, ez;
        sphereToCart(theta, phi, rho, &ex, &ey, &ez);
        glLookAt(ex + 0.5, ey, ez + 0.5, /* Eye */
                  0.5, 0.0, 0.5, /* Center */
                  0, 0, 1); /* Up */
    }

    // Step 2 :
    // Data Buffering and drawing
    //
    for(unsigned i = 0; i < waterfall_verts.size(); i++) { // Loop through all traces
        std::vector<float> &r = *waterfall_verts[i]; // The trace
        std::vector<float> &t = *waterfall_coords[i]; // Trace tex coords

        // Buffer the data, each 2D and 3D handle setting pointers
        glBindBuffer(GL_ARRAY_BUFFER, textureVBO);
        glBufferData(GL_ARRAY_BUFFER, t.size() * sizeof(float),
                     &t[0], GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, traceVBO);
        glBufferData(GL_ARRAY_BUFFER, r.size() * sizeof(float),
                     &r[0], GL_DYNAMIC_DRAW);

        if(waterfall_state == Waterfall2D) {
            // Reset glPointers, draw line across top of trace, then shift
            glLineWidth(5.0);
            glBindBuffer(GL_ARRAY_BUFFER, textureVBO);
            glTexCoordPointer(2, GL_FLOAT, 16, (GLvoid*)0);
            glBindBuffer(GL_ARRAY_BUFFER, traceVBO);
            glVertexPointer(3, GL_FLOAT, 24, (GLvoid*)0);
            glDrawArrays(GL_LINE_STRIP, 0, r.size() / 6);
            glTranslatef(0.0, 4.0f, 0.0);
            glLineWidth(1.0);

        } else if (waterfall_state == Waterfall3D) {
            // Main draw and pointers
            glBindBuffer(GL_ARRAY_BUFFER, textureVBO);
            glTexCoordPointer(2, GL_FLOAT, 0, (GLvoid*)0);
            glBindBuffer(GL_ARRAY_BUFFER, traceVBO);
            glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)0);
            glDrawArrays(GL_QUAD_STRIP, 0, r.size() / 3);

            // Draw the waterfall outline
            glDisable(GL_TEXTURE_2D);
            glColor3f(0.0, 0.0, 0.0);
            glBindBuffer(GL_ARRAY_BUFFER, traceVBO);
            glVertexPointer(3, GL_FLOAT, 24, (GLvoid*)0);
            glDrawArrays(GL_LINE_STRIP, 0, r.size() / 6);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glEnable(GL_TEXTURE_2D);

            glTranslatef(0, 0.05f, 0);
        }
    }

    // Step 3 :
    // Clean up/Revert GL state
    //
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_TEXTURE_2D);
    //glDisable(GL_LINE_SMOOTH);
    //glDisable(GL_BLEND);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glPopAttrib();
    glDisable(GL_DEPTH_TEST);
}

//void TraceView::DrawPersistence()
//{
//    static bool first_time = true;
//    static GLuint persistTex;

//    if(first_time) {
//        glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
//        // Render buffer start, build the tex we are writing to
//        glGenTextures( 1 , &persistTex );
//        glBindTexture( GL_TEXTURE_2D , persistTex );
//        glTexParameteri( GL_TEXTURE_2D , GL_TEXTURE_WRAP_S , GL_CLAMP );
//        glTexParameteri( GL_TEXTURE_2D , GL_TEXTURE_WRAP_T , GL_CLAMP );
//        glTexParameteri( GL_TEXTURE_2D , GL_TEXTURE_MAG_FILTER , GL_LINEAR );
//        glTexParameteri( GL_TEXTURE_2D , GL_TEXTURE_MIN_FILTER , GL_LINEAR );
//        glBindTexture( 1, 0 );

//        first_time = false;
//    }

//    // Prep viewport
//    glViewport(grat_ll.x(), grat_ll.y(),
//               grat_sz.x(), grat_sz.y());

//    // Prep modelview
//    glMatrixMode(GL_MODELVIEW);
//    glLoadIdentity();

//    // Ortho
//    glMatrixMode(GL_PROJECTION);
//    glLoadIdentity();
//    glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);

//    Persistence *p = session_ptr->trace_manager->GetPersistence();

//    glEnable(GL_TEXTURE_2D);
//    glBindTexture(GL_TEXTURE_2D, persistTex);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

//    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p->Width(), p->Height(), 0,
//        GL_RGBA, GL_FLOAT, (unsigned char*)p->GetImage());

//    // Draw a single quad over our grat
//    glUseProgram(persist_program->Handle());
//    //glBindTexture(GL_TEXTURE_2D, persistTex);
//    //glGenerateMipmap(GL_TEXTURE_2D);

////    glMatrixMode(GL_MODELVIEW);
////    glPushMatrix();
////    glLoadIdentity();
////    glTranslatef((float)xMargin, (float)botYMargin, 0.0f);
////    glScalef((float)tenXgrat, (float)tenYgrat, 1.0f);

//    glEnable(GL_BLEND);
//    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

//    glBegin(GL_QUADS);
//    glTexCoord2f(0,0); glVertex2f(0,0);
//    glTexCoord2f(0,1); glVertex2f(0,1);
//    glTexCoord2f(1,1); glVertex2f(1,1);
//    glTexCoord2f(1,0); glVertex2f(1,0);
//    glEnd();

//    glDisable(GL_BLEND);
//    glDisable(GL_TEXTURE_2D);

//    glMatrixMode(GL_MODELVIEW);
//    glLoadIdentity();
//    glMatrixMode(GL_PROJECTION);
//    glLoadIdentity();

//    glUseProgram(0);
//}