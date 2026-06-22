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

// ─── UDP Constructor ───────────────────────────────────────────────────────────
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

    startNewSegment();
}

// ─── Yerel Kamera Constructor ──────────────────────────────────────────────────
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

    startNewSegment();
    camera->start();

    qDebug() << "[LocalCam] Başlatıldı:" << device.description();
}

// ─── Kimlik ───────────────────────────────────────────────────────────────────
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

// ─── Yüz tanıma kurulumu ──────────────────────────────────────────────────────
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
        // Üzerinde kutu kalmasın diye son kareyi temiz bas
        if (!m_lastFrame.isNull())
            liveLabel->setPixmap(QPixmap::fromImage(m_lastFrame));
    } else {
        m_submitTimer.invalidate();   // hemen ilk kareyi gönderebil
    }
}

void CameraFeed::onFaceResults(int feedId, const QVector<FaceResult> &results)
{
    if (feedId != m_feedId) return;   // başka kameranın sonucu, bizi ilgilendirmez
    m_latestResults = results;
    m_faceBusy = false;
}

// ─── UI Kurulumu ──────────────────────────────────────────────────────────────
void CameraFeed::setupUI()
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("CameraFeed { background-color: #111; }");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── Video alanı ─────────────────────────────────────────────────────────
    liveLabel = new QLabel(this);
    liveLabel->setAlignment(Qt::AlignCenter);
    liveLabel->setScaledContents(true);
    liveLabel->setMinimumSize(1, 1);
    liveLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    liveLabel->setStyleSheet("background-color: #000; color: #666; font-size: 11pt;");
    mainLayout->addWidget(liveLabel, 1);

    // ── Kompakt kontrol çubuğu ──────────────────────────────────────────────
    QWidget *ctrlBar = new QWidget(this);
    ctrlBar->setFixedHeight(30);
    ctrlBar->setStyleSheet("background-color: #1c1c1c;");

    QHBoxLayout *ctrlLayout = new QHBoxLayout(ctrlBar);
    ctrlLayout->setContentsMargins(4, 3, 4, 3);
    ctrlLayout->setSpacing(4);

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

    ctrlLayout->addWidget(pauseBtn);
    ctrlLayout->addWidget(slider, 1);
    ctrlLayout->addWidget(timeEdit);
    ctrlLayout->addWidget(watchBtn);
    ctrlLayout->addWidget(liveBtn);

    mainLayout->addWidget(ctrlBar);
}

void CameraFeed::setupTimers()
{
    segmentTimer = new QTimer(this);
    connect(segmentTimer, &QTimer::timeout, this, &CameraFeed::newSegment);
    segmentTimer->start(60 * 1000);

    playbackTimer = new QTimer(this);
    connect(playbackTimer, &QTimer::timeout, this, &CameraFeed::nextPlaybackFrame);
}

void CameraFeed::setupSignals()
{
    connect(watchBtn, &QPushButton::clicked,   this, &CameraFeed::onWatchClicked);
    connect(liveBtn,  &QPushButton::clicked,   this, &CameraFeed::onLiveClicked);
    connect(pauseBtn, &QPushButton::clicked,   this, &CameraFeed::onPauseClicked);
    connect(slider,   &QSlider::valueChanged,  this, &CameraFeed::onSliderChanged);
    connect(timeEdit, &QTimeEdit::timeChanged, this, &CameraFeed::onTimeChanged);
}

// ─── UDP Datagram ─────────────────────────────────────────────────────────────
void CameraFeed::readData()
{
    while (udpSocket->hasPendingDatagrams()) {
        auto datagram = udpSocket->receiveDatagram();
        auto data     = datagram.data();

        // TEŞHİS: bu porta gönderen her farklı IP'yi bir kez logla
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
        if (!image.loadFromData(data)) continue;   // çözülemeyen kareyi atla

        if (!wasEverActive) {
            wasEverActive = true;
            liveLabel->setStyleSheet("background-color: #000;");
            emit firstFrameReceived();
        }

        if (!isPlayback) {
            renderFrame(image);
            maybeSubmit(image);
        }
        saveFrame(data);   // ham JPEG baytları (yeniden kodlama yok)
    }
}

// ─── Yerel Kamera Frame ───────────────────────────────────────────────────────
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

    if (!isPlayback) {
        renderFrame(image);
        maybeSubmit(image);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", 60)) return;
    saveFrame(bytes);
}

// ─── Görüntüyü ekrana bas (varsa yüz kutularını çiz) ──────────────────────────
void CameraFeed::renderFrame(const QImage &img)
{
    m_lastFrame = img;   // enrollment/snapshot için ham kareyi sakla

    if (!m_faceEnabled || m_latestResults.isEmpty()) {
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

// ─── Motora kare gönder (kısıtlı + meşgulse atla) ─────────────────────────────
void CameraFeed::maybeSubmit(const QImage &img)
{
    if (!m_faceEnabled || !m_engine) return;
    if (m_faceBusy) return;
    if (m_submitTimer.isValid() && m_submitTimer.elapsed() < MIN_SUBMIT_INTERVAL_MS)
        return;

    m_faceBusy = true;
    m_submitTimer.restart();

    // Worker thread'e queued çağrı; QImage implicit-shared olduğu için kopya ucuz
    QMetaObject::invokeMethod(m_engine, "processFrame", Qt::QueuedConnection,
                              Q_ARG(int, m_feedId),
                              Q_ARG(QImage, img));
}

// ─── Kayıt ────────────────────────────────────────────────────────────────────
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
}

// ─── startNewSegment ─────────────────────────────────────────────────────────
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
    segmentFiles.enqueue(currentSegDir);
    if (segmentFiles.size() > MAX_SEGMENTS) {
        QString oldest = segmentFiles.dequeue();
        QDir(oldest).removeRecursively();
    }
}

void CameraFeed::newSegment() { startNewSegment(); }

// ─── Oynatma ──────────────────────────────────────────────────────────────────
void CameraFeed::collectPlaybackFrames()
{
    playbackFrames.clear();
    for (const QString &segDir : segmentFiles) {
        QDir dir(segDir);
        QStringList frames = dir.entryList({"*.jpg"}, QDir::Files, QDir::Name);
        for (const QString &frame : frames)
            playbackFrames.append(segDir + "/" + frame);
    }
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
    startNewSegment();

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

CameraFeed::~CameraFeed() {}
