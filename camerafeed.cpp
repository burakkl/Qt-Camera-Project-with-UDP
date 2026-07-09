#include "camerafeed.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QPixmap>
#include <QImage>
#include <QBuffer>
#include <QPainter>
#include <QFontMetrics>
#include <QRegularExpression>
#include <QComboBox>
#include <QResizeEvent>
#include <QMap>
#include <algorithm>

CameraFeed::CameraFeed(int port, QWidget *parent)
    : QWidget(parent)
    , mode(Mode::UDP)
    , port(port)
    , frameCount(0)
    , playbackIndex(0)
    , isPlayback(false)
    , isPaused(false)
    , wasEverActive(false)
    , udpSocket(nullptr)
    , camera(nullptr)
    , session(nullptr)
    , videoSink(nullptr)
{
    setupUI();
    setupTimers();
    setupSignals();

    liveLabel->setText(QString("Port %1\nBağlantı bekleniyor...").arg(port));

    udpSocket = new QUdpSocket(this);
    if (!udpSocket->bind(QHostAddress::Any, port))
        qDebug() << "[UDP" << port << "] BIND BAŞARISIZ:" << udpSocket->errorString();
    else
        qDebug() << "[UDP" << port << "] Dinleniyor.";
    connect(udpSocket, &QUdpSocket::readyRead, this, &CameraFeed::readData);
}

CameraFeed::CameraFeed(const QCameraDevice &device, QWidget *parent)
    : QWidget(parent)
    , mode(Mode::Local)
    , port(-1)
    , frameCount(0)
    , playbackIndex(0)
    , isPlayback(false)
    , isPaused(false)
    , wasEverActive(false)
    , udpSocket(nullptr)
{
    setupUI();
    setupTimers();
    setupSignals();

    camera    = new QCamera(device, this);
    session   = new QMediaCaptureSession(this);
    videoSink = new QVideoSink(this);

    session->setCamera(camera);
    session->setVideoSink(videoSink);

    connect(videoSink, &QVideoSink::videoFrameChanged,
            this, &CameraFeed::onLocalFrame);

    camera->start();

    qDebug() << "[LocalCam] Başlatıldı:" << device.description();
}

QString CameraFeed::cameraDeviceId() const
{
    if (mode == Mode::Local && camera)
        return QString::fromLatin1(camera->cameraDevice().id());
    return {};
}

QString CameraFeed::cameraDescription() const
{
    if (mode == Mode::Local && camera)
        return camera->cameraDevice().description();
    return {};
}

int CameraFeed::udpPort() const
{
    return (mode == Mode::UDP) ? port : -1;
}

void CameraFeed::setFaceEngine(FaceEngine *engine, int feedId)
{
    m_engine = engine;
    m_feedId = feedId;
    if (m_engine)
        connect(m_engine, &FaceEngine::resultsReady,
                this, &CameraFeed::onFaceResults);
}

void CameraFeed::setFaceRecognition(bool on)
{
    m_faceEnabled = on;
    if (!on) {
        m_latestResults.clear();
        m_faceBusy = false;
        if (!m_lastFrame.isNull())
            liveLabel->setPixmap(QPixmap::fromImage(m_lastFrame));
    } else {
        m_submitTimer.invalidate();
    }
}

void CameraFeed::onFaceResults(int feedId, const QVector<FaceResult> &results)
{
    if (feedId != m_feedId) return;
    m_latestResults = results;
    m_faceBusy = false;
}

void CameraFeed::setupUI()
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("CameraFeed { background-color: #111; }");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    liveLabel = new QLabel(this);
    liveLabel->setAlignment(Qt::AlignCenter);
    liveLabel->setScaledContents(true);
    liveLabel->setMinimumSize(1, 1);
    liveLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    liveLabel->setStyleSheet("background-color: #000; color: #666; font-size: 11pt;");
    mainLayout->addWidget(liveLabel, 1);

    m_recBadge = new QLabel("● REC", liveLabel);
    m_recBadge->setStyleSheet(
        "QLabel { background: rgba(0,0,0,160); color: #e62828;"
        " font-weight: bold; padding: 3px 8px; border-radius: 4px; }");
    m_recBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_recBadge->adjustSize();
    m_recBadge->move(8, 8);
    m_recBadge->hide();

    m_clockLabel = new QLabel(liveLabel);
    m_clockLabel->setStyleSheet(
        "QLabel { background: rgba(0,0,0,140); color: #eaeaea;"
        " font-weight: bold; padding: 2px 7px; border-radius: 4px; }");
    m_clockLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_clockLabel->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));
    m_clockLabel->adjustSize();
    m_clockLabel->move(8, 8);

    QWidget *ctrlBar = new QWidget(this);
    ctrlBar->setFixedHeight(30);
    ctrlBar->setStyleSheet("background-color: #1c1c1c;");

    QHBoxLayout *ctrlLayout = new QHBoxLayout(ctrlBar);
    ctrlLayout->setContentsMargins(4, 3, 4, 3);
    ctrlLayout->setSpacing(4);

    motionDot = new QLabel(ctrlBar);
    motionDot->setFixedSize(14, 14);
    motionDot->setStyleSheet("background:#555; border-radius:7px;");
    motionDot->setToolTip("Hareket durumu");

    recordBtn = new QPushButton("⏺", ctrlBar);
    recordBtn->setFixedSize(26, 22);
    recordBtn->setCheckable(true);
    recordBtn->setToolTip("Kaydı elle başlat (hareketten bağımsız)");
    recordBtn->setStyleSheet(
        "QPushButton{background:#2a2a2a;color:#ddd;border:none;border-radius:3px;}"
        "QPushButton:hover{background:#3a3a3a;}"
        "QPushButton:checked{background:#c0392b;color:#fff;}");

    pauseBtn = new QPushButton("⏸", ctrlBar);
    pauseBtn->setFixedSize(26, 22);
    pauseBtn->setVisible(false);

    slider = new QSlider(Qt::Horizontal, ctrlBar);
    slider->setVisible(false);

    timeEdit = new QTimeEdit(ctrlBar);
    timeEdit->setDisplayFormat("mm:ss");
    timeEdit->setFixedWidth(54);
    timeEdit->setFixedHeight(22);
    timeEdit->setVisible(false);

    watchBtn = new QPushButton("⏺ Kaydı İzle", ctrlBar);
    watchBtn->setFixedHeight(22);

    liveBtn = new QPushButton("▶ Canlı", ctrlBar);
    liveBtn->setFixedHeight(22);
    liveBtn->setVisible(false);

    ctrlLayout->addWidget(motionDot);
    ctrlLayout->addWidget(recordBtn);
    ctrlLayout->addWidget(pauseBtn);
    ctrlLayout->addWidget(slider, 1);
    ctrlLayout->addWidget(timeEdit);
    ctrlLayout->addWidget(watchBtn);
    ctrlLayout->addWidget(liveBtn);

    mainLayout->addWidget(ctrlBar);

    faceBar = new QWidget(this);
    faceBar->setFixedHeight(28);
    faceBar->setStyleSheet("background-color:#181818;");

    QHBoxLayout *fb = new QHBoxLayout(faceBar);
    fb->setContentsMargins(6, 2, 6, 2);
    fb->setSpacing(5);

    QLabel *fbLabel = new QLabel("Yüz:", faceBar);
    fbLabel->setStyleSheet("color:#bbb;");

    m_personCombo = new QComboBox(faceBar);
    m_personCombo->setMinimumWidth(120);
    m_personCombo->setStyleSheet(
        "QComboBox{background:#2a2a2a;color:#eee;border:1px solid #3a3a3a;"
        "border-radius:3px;padding:1px 4px;}");

    m_prevFaceBtn = new QPushButton("◀ Önceki", faceBar);
    m_nextFaceBtn = new QPushButton("Sonraki ▶", faceBar);
    for (QPushButton *b : {m_prevFaceBtn, m_nextFaceBtn}) {
        b->setFixedHeight(22);
        b->setStyleSheet(
            "QPushButton{background:#2a2a2a;color:#ddd;border:none;"
            "border-radius:3px;padding:0 8px;}"
            "QPushButton:hover{background:#3a3a3a;}"
            "QPushButton:disabled{color:#666;}");
    }

    m_faceJumpInfo = new QLabel(faceBar);
    m_faceJumpInfo->setStyleSheet("color:#888;");

    fb->addWidget(fbLabel);
    fb->addWidget(m_personCombo);
    fb->addWidget(m_prevFaceBtn);
    fb->addWidget(m_nextFaceBtn);
    fb->addWidget(m_faceJumpInfo);
    fb->addStretch(1);

    faceBar->setVisible(false);
    mainLayout->addWidget(faceBar);
}

void CameraFeed::setupTimers()
{
    segmentTimer = new QTimer(this);
    connect(segmentTimer, &QTimer::timeout, this, &CameraFeed::newSegment);

    recordWatchdog = new QTimer(this);
    connect(recordWatchdog, &QTimer::timeout, this, &CameraFeed::recordWatchdogTick);

    playbackTimer = new QTimer(this);
    connect(playbackTimer, &QTimer::timeout, this, &CameraFeed::nextPlaybackFrame);

    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, [this]{
        if (!m_clockLabel) return;
        m_clockLabel->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));
        m_clockLabel->adjustSize();
        m_clockLabel->move(liveLabel->width() - m_clockLabel->width() - 8, 8);
    });
    m_clockTimer->start();
}

void CameraFeed::setupSignals()
{
    connect(watchBtn, &QPushButton::clicked,   this, &CameraFeed::onWatchClicked);
    connect(liveBtn,  &QPushButton::clicked,   this, &CameraFeed::onLiveClicked);
    connect(pauseBtn, &QPushButton::clicked,   this, &CameraFeed::onPauseClicked);
    connect(slider,   &QSlider::valueChanged,  this, &CameraFeed::onSliderChanged);
    connect(timeEdit, &QTimeEdit::timeChanged, this, &CameraFeed::onTimeChanged);
    connect(recordBtn, &QPushButton::toggled,  this, &CameraFeed::onRecordButtonToggled);
    connect(m_prevFaceBtn, &QPushButton::clicked, this, &CameraFeed::onPrevFace);
    connect(m_nextFaceBtn, &QPushButton::clicked, this, &CameraFeed::onNextFace);
}

void CameraFeed::readData()
{
    while (udpSocket->hasPendingDatagrams()) {
        auto datagram = udpSocket->receiveDatagram();
        auto data     = datagram.data();

        QString senderKey = datagram.senderAddress().toString()
                            + ":" + QString::number(datagram.senderPort());
        if (!seenSenders.contains(senderKey)) {
            seenSenders.insert(senderKey);
            qDebug() << "[UDP" << port << "] Yeni gönderici:" << senderKey
                     << "| bu porta gönderen toplam:" << seenSenders.size();
            if (seenSenders.size() > 1)
                qDebug() << "  ⚠️ DİKKAT: port" << port
                         << "birden fazla kaynaktan veri alıyor! Görüntüler karışır.";
        }

        QImage image;
        if (!image.loadFromData(data)) continue;

        if (!wasEverActive) {
            wasEverActive = true;
            liveLabel->setStyleSheet("background-color: #000;");
            emit firstFrameReceived();
        }

        updateMotion(image);

        if (!isPlayback) {
            renderFrame(image);
            maybeSubmit(image);
        }
        if (m_recording)
            saveFrame(data);
    }
}

void CameraFeed::onLocalFrame(const QVideoFrame &frame)
{
    QVideoFrame f = frame;
    if (!f.isValid()) return;

    QImage image = f.toImage();

    if (image.isNull() && f.map(QVideoFrame::ReadOnly)) {
        QImage::Format fmt = QVideoFrameFormat::imageFormatFromPixelFormat(
            f.surfaceFormat().pixelFormat());
        if (fmt != QImage::Format_Invalid)
            image = QImage(f.bits(0), f.width(), f.height(),
                           f.bytesPerLine(0), fmt).copy();
        f.unmap();
    }

    if (image.isNull()) return;

    updateMotion(image);

    if (!isPlayback) {
        renderFrame(image);
        maybeSubmit(image);
    }

    if (m_recording) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "JPEG", 60)) return;
        saveFrame(bytes);
    }
}

void CameraFeed::renderFrame(const QImage &img)
{
    m_lastFrame = img;

    const bool haveFaces = m_faceEnabled && !m_latestResults.isEmpty();

    if (!haveFaces) {
        liveLabel->setPixmap(QPixmap::fromImage(img));
        return;
    }

    QImage canvas = img.convertToFormat(QImage::Format_RGB32);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing, true);

    QFont font = p.font();
    font.setPointSize(qMax(10, canvas.height() / 40));
    font.setBold(true);
    p.setFont(font);
    QFontMetrics fm(font);

    const int penW = qMax(2, canvas.width() / 320);

    for (const FaceResult &fr : m_latestResults) {
        QColor col = fr.known ? QColor(40, 200, 90) : QColor(230, 70, 70);

        p.setPen(QPen(col, penW));
        p.setBrush(Qt::NoBrush);
        p.drawRect(fr.box);

        QString label = fr.known
                            ? QString("%1  %%2").arg(fr.name).arg(int(fr.confidence * 100))
                            : fr.name;

        QRect textRect = fm.boundingRect(label).adjusted(-6, -3, 6, 3);
        textRect.moveTopLeft(QPoint(fr.box.left(), fr.box.top() - textRect.height()));
        if (textRect.top() < 0)
            textRect.moveTop(fr.box.bottom());

        p.fillRect(textRect, col);
        p.setPen(Qt::white);
        p.drawText(textRect, Qt::AlignCenter, label);
    }
    p.end();

    liveLabel->setPixmap(QPixmap::fromImage(canvas));
}

void CameraFeed::maybeSubmit(const QImage &img)
{
    if (!m_faceEnabled || !m_engine) return;
    if (m_faceBusy) return;
    if (m_submitTimer.isValid() && m_submitTimer.elapsed() < MIN_SUBMIT_INTERVAL_MS)
        return;

    m_faceBusy = true;
    m_submitTimer.restart();

    QMetaObject::invokeMethod(m_engine, "processFrame", Qt::QueuedConnection,
                              Q_ARG(int, m_feedId),
                              Q_ARG(QImage, img));
}

void CameraFeed::saveFrame(const QByteArray &data)
{
    frameCount++;
    QString path = QString("%1/%2.jpg")
                       .arg(currentSegDir)
                       .arg(frameCount, 6, 10, QChar('0'));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "[saveFrame] Açılamadı:" << path;
        return;
    }
    file.write(data);
    file.close();

    logFaceAppearances();
}

void CameraFeed::startNewSegment()
{
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString cam;

    if (mode == Mode::Local) {
        QString desc = cameraDescription();
        if (desc.isEmpty()) desc = "local_cam";
        desc.replace(QRegularExpression("[^A-Za-z0-9]"), "_");
        desc.replace(QRegularExpression("_+"), "_");
        if (desc.length() > 24) desc = desc.left(24);
        while (!desc.isEmpty() && desc.startsWith('_')) desc.remove(0, 1);
        while (!desc.isEmpty() && desc.endsWith('_'))   desc.chop(1);
        if (desc.isEmpty()) desc = "local_cam";
        cam = desc;
    } else {
        cam = QString("camera_%1").arg(port);
    }

    currentSegDir = QString("C:/kamera_proje/%1/seg_%2").arg(cam).arg(ts);

    if (!QDir().mkpath(currentSegDir))
        qDebug() << "[startNewSegment] Klasör oluşturulamadı:" << currentSegDir;
    else
        qDebug() << "[startNewSegment] Kayıt:" << currentSegDir;

    frameCount = 0;
    m_lastSeenFrame.clear();
    segmentFiles.enqueue(currentSegDir);
    if (segmentFiles.size() > MAX_SEGMENTS) {
        QString oldest = segmentFiles.dequeue();
        QDir(oldest).removeRecursively();
    }
}

void CameraFeed::newSegment()
{
    if (m_recording)
        startNewSegment();
}

void CameraFeed::collectPlaybackFrames()
{
    playbackFrames.clear();
    m_faceMarkers.clear();

    for (const QString &segDir : segmentFiles) {
        const int segStart = playbackFrames.size();

        QDir dir(segDir);
        QStringList frames = dir.entryList({"*.jpg"}, QDir::Files, QDir::Name);
        for (const QString &frame : frames)
            playbackFrames.append(segDir + "/" + frame);

        QFile log(segDir + "/faces.log");
        if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!log.atEnd()) {
                const QByteArray line = log.readLine().trimmed();
                if (line.isEmpty()) continue;
                const int comma = line.indexOf(',');
                if (comma <= 0) continue;

                bool ok = false;
                const int frameNo = line.left(comma).toInt(&ok);
                if (!ok) continue;

                const QString name = QString::fromUtf8(line.mid(comma + 1));
                const QString fileName =
                    QString("%1.jpg").arg(frameNo, 6, 10, QChar('0'));
                const int pos = frames.indexOf(fileName);
                if (pos >= 0)
                    m_faceMarkers.append({ segStart + pos, name });
            }
            log.close();
        }
    }

    std::sort(m_faceMarkers.begin(), m_faceMarkers.end(),
              [](const FaceMarker &a, const FaceMarker &b){ return a.index < b.index; });
}

void CameraFeed::showFrame(int index)
{
    if (index < 0 || index >= playbackFrames.size()) return;
    QPixmap px(playbackFrames[index]);
    if (!px.isNull()) liveLabel->setPixmap(px);

    int totalSecs = index / 30;
    timeEdit->blockSignals(true);
    timeEdit->setTime(QTime(0, totalSecs / 60, totalSecs % 60));
    timeEdit->blockSignals(false);
}

void CameraFeed::goToPlayback()
{
    collectPlaybackFrames();
    if (playbackFrames.isEmpty()) {
        qDebug() << "[goToPlayback] Kayıtlı frame yok.";
        return;
    }
    isPlayback = true;

    populateFaceJump();
    faceBar->setVisible(true);

    slider->setMaximum(playbackFrames.size() - 1);
    slider->setValue(0);
    slider->setVisible(true);
    pauseBtn->setText("⏸");
    pauseBtn->setVisible(true);
    timeEdit->setVisible(true);
    watchBtn->setVisible(false);
    liveBtn->setVisible(true);
    isPaused = false;

    playbackIndex = 0;
    showFrame(0);
    playbackTimer->start(33);
}

void CameraFeed::goToLive()
{
    isPlayback = false;
    playbackTimer->stop();
    isPaused = false;
    slider->setVisible(false);
    pauseBtn->setVisible(false);
    timeEdit->setVisible(false);
    liveBtn->setVisible(false);
    watchBtn->setVisible(true);
    faceBar->setVisible(false);
}

void CameraFeed::onWatchClicked() { goToPlayback(); }
void CameraFeed::onLiveClicked()  { goToLive(); }

void CameraFeed::onPauseClicked()
{
    if (isPaused) {
        isPaused = false;
        pauseBtn->setText("⏸");
        playbackTimer->start(33);
    } else {
        isPaused = true;
        pauseBtn->setText("▶");
        playbackTimer->stop();
    }
}

void CameraFeed::onSliderChanged(int value)
{
    playbackTimer->stop();
    playbackIndex = value;
    showFrame(value);
    if (!isPaused) playbackTimer->start(33);
}

void CameraFeed::onTimeChanged(const QTime &time)
{
    int totalSecs  = time.minute() * 60 + time.second();
    int frameIndex = qMin(totalSecs * 30, playbackFrames.size() - 1);

    slider->blockSignals(true);
    slider->setValue(frameIndex);
    slider->blockSignals(false);

    playbackIndex = frameIndex;
    showFrame(frameIndex);
}

void CameraFeed::nextPlaybackFrame()
{
    if (playbackIndex >= playbackFrames.size() - 1) {
        playbackTimer->stop();
        return;
    }
    playbackIndex++;
    slider->blockSignals(true);
    slider->setValue(playbackIndex);
    slider->blockSignals(false);
    showFrame(playbackIndex);
}


bool CameraFeed::detectMotionInFrame(const QImage &liveImg)
{
    QImage small = liveImg.scaled(MOTION_W, MOTION_H,
                                  Qt::IgnoreAspectRatio,
                                  Qt::FastTransformation)
                       .convertToFormat(QImage::Format_Grayscale8);

    if (m_prevGray.isNull()
        || m_prevGray.width()  != small.width()
        || m_prevGray.height() != small.height()) {
        m_prevGray = small;
        return false;
    }

    const int w = small.width();
    const int h = small.height();
    int changed = 0;
    for (int y = 0; y < h; ++y) {
        const uchar *cur = small.constScanLine(y);
        const uchar *prv = m_prevGray.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            int d = int(cur[x]) - int(prv[x]);
            if (d < 0) d = -d;
            if (d > MOTION_PIXEL_DELTA) ++changed;
        }
    }
    m_prevGray = small;

    const double frac = double(changed) / double(w * h);
    return frac >= MOTION_AREA_FRAC;
}

void CameraFeed::updateMotion(const QImage &liveImg)
{
    if (m_motionSampleTimer.isValid()
        && m_motionSampleTimer.elapsed() < MOTION_SAMPLE_MS)
        return;
    m_motionSampleTimer.restart();

    const bool motion = detectMotionInFrame(liveImg);

    if (motion) {
        m_lastMotionTimer.restart();
        if (!m_recording)
            startRecording();
    }

    const bool show = motion
                      || (m_lastMotionTimer.isValid()
                          && m_lastMotionTimer.elapsed() < MOTION_HOLD_MS);
    setMotionIndicator(show);
}

void CameraFeed::setMotionIndicator(bool active)
{
    const int s = active ? 1 : 0;
    if (s == m_motionState) return;
    m_motionState = s;
    if (!motionDot) return;

    if (active) {
        motionDot->setStyleSheet("background:#27c93f; border-radius:7px;");
        motionDot->setToolTip("Hareket algılandı");
    } else {
        motionDot->setStyleSheet("background:#9b2f2a; border-radius:7px;");
        motionDot->setToolTip("Hareket yok");
    }
}

void CameraFeed::startRecording()
{
    if (m_recording) return;
    m_recording = true;

    startNewSegment();
    segmentTimer->start(60 * 1000);
    recordWatchdog->start(1000);

    liveLabel->setStyleSheet(
        "QLabel { background-color: #000; border: 2px solid #c0392b; }");
    if (m_recBadge) {
        m_recBadge->show();
        m_recBadge->raise();
    }

    updateRecordButton();
    qDebug() << "[Rec] BAŞLADI:"
             << (mode == Mode::UDP ? QString("port %1").arg(port)
                                   : cameraDescription());
}

void CameraFeed::stopRecording()
{
    if (!m_recording) return;
    m_recording = false;

    segmentTimer->stop();
    recordWatchdog->stop();

    if (m_recBadge)      m_recBadge->hide();
    liveLabel->setStyleSheet(
        "background-color: #000; color: #666; font-size: 11pt;");

    updateRecordButton();
    qDebug() << "[Rec] DURDU.";
}

void CameraFeed::recordWatchdogTick()
{
    if (!m_recording || m_manualRecord) return;
    if (m_lastMotionTimer.isValid()
        && m_lastMotionTimer.elapsed() >= RECORD_LINGER_MS)
        stopRecording();
}

void CameraFeed::onRecordButtonToggled(bool on)
{
    m_manualRecord = on;
    if (on) {
        startRecording();
    } else {
        if (!m_lastMotionTimer.isValid()
            || m_lastMotionTimer.elapsed() >= RECORD_LINGER_MS)
            stopRecording();
    }
    updateRecordButton();
}

void CameraFeed::updateRecordButton()
{
    if (!recordBtn) return;
    recordBtn->blockSignals(true);
    recordBtn->setChecked(m_manualRecord);
    recordBtn->blockSignals(false);

    recordBtn->setToolTip(
        m_recording
            ? (m_manualRecord ? "Elle kayıt sürüyor — durdurmak için tıkla"
                              : "Hareketle kayıt sürüyor — elle kayda almak için tıkla")
            : "Kaydı elle başlat (hareketten bağımsız)");
}


void CameraFeed::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_clockLabel) {
        m_clockLabel->adjustSize();
        m_clockLabel->move(liveLabel->width() - m_clockLabel->width() - 8, 8);
    }
    if (m_recBadge)
        m_recBadge->move(8, 8);
}

void CameraFeed::logFaceAppearances()
{
    if (!m_faceEnabled) return;

    for (const FaceResult &fr : m_latestResults) {
        if (!fr.known || fr.name.isEmpty()) continue;

        const int last = m_lastSeenFrame.value(fr.name, -1000000);
        if (frameCount - last > FACE_REAPPEAR_GAP_FRAMES)
            appendFaceLog(frameCount, fr.name);

        m_lastSeenFrame[fr.name] = frameCount;
    }
}

void CameraFeed::appendFaceLog(int frameNo, const QString &name)
{
    QString clean = name;
    clean.replace(',', ' ').replace('\n', ' ').replace('\r', ' ');

    QFile f(currentSegDir + "/faces.log");
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write(QString("%1,%2\n").arg(frameNo).arg(clean).toUtf8());
}

void CameraFeed::populateFaceJump()
{
    if (!m_personCombo) return;

    m_personCombo->blockSignals(true);
    m_personCombo->clear();

    QMap<QString,int> counts;
    for (const FaceMarker &m : m_faceMarkers)
        counts[m.name] += 1;

    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        m_personCombo->addItem(QString("%1  (%2)").arg(it.key()).arg(it.value()),
                               it.key());

    m_personCombo->blockSignals(false);

    const bool have = m_personCombo->count() > 0;
    m_prevFaceBtn->setEnabled(have);
    m_nextFaceBtn->setEnabled(have);
    if (m_faceJumpInfo)
        m_faceJumpInfo->setText(have ? QString() : "kayıtta tanınan yüz yok");
}

void CameraFeed::onPrevFace() { jumpToFace(-1); }
void CameraFeed::onNextFace() { jumpToFace(+1); }

void CameraFeed::jumpToFace(int dir)
{
    if (!m_personCombo || m_personCombo->count() == 0) return;
    const QString person = m_personCombo->currentData().toString();

    QList<int> idxs;
    for (const FaceMarker &m : m_faceMarkers)
        if (m.name == person) idxs.append(m.index);
    if (idxs.isEmpty()) return;
    std::sort(idxs.begin(), idxs.end());

    int target = -1;
    if (dir > 0) {
        for (int f : idxs) if (f > playbackIndex) { target = f; break; }
        if (target < 0) target = idxs.first();
    } else {
        for (int i = idxs.size() - 1; i >= 0; --i)
            if (idxs[i] < playbackIndex) { target = idxs[i]; break; }
        if (target < 0) target = idxs.last();
    }
    seekTo(target);
}

void CameraFeed::seekTo(int index)
{
    if (playbackFrames.isEmpty()) return;
    index = qBound(0, index, playbackFrames.size() - 1);

    playbackTimer->stop();
    isPaused = true;
    pauseBtn->setText("▶");

    playbackIndex = index;
    slider->blockSignals(true);
    slider->setValue(index);
    slider->blockSignals(false);
    showFrame(index);

    if (m_faceJumpInfo) {
        const int secs = index / 30;
        m_faceJumpInfo->setText(QString("→ %1:%2")
                                    .arg(secs / 60, 2, 10, QChar('0'))
                                    .arg(secs % 60, 2, 10, QChar('0')));
    }
}

CameraFeed::~CameraFeed() {}