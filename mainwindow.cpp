#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "faceengine.h"
#include "enrolldialog.h"

#include <QVBoxLayout>
#include <QTimer>
#include <QMap>
#include <QThread>
#include <QToolBar>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QCoreApplication>
#include <QFile>
#include <QDir>

static const QList<int> CAMERA_PORTS = {5000, 5001, 5002, 5003};

// Model klasörünü çöz: önce exe yanındaki "models", sonra C:/kamera_proje/models
static QString resolveModelsDir()
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/models",
        "C:/kamera_proje/models",
    };
    for (const QString &d : candidates)
        if (QFile::exists(d + "/face_detection_yunet_2023mar.onnx"))
            return d;
    return candidates.first();   // bulunamadıysa motor anlamlı hata verecek
}

// ─── Yardımcı: gridLayout'un TÜM içeriğini temizler ──────────────────────────
static void clearGridLayout(QGridLayout *layout, QList<QWidget*> &placeholders)
{
    while (layout->count() > 0) {
        QLayoutItem *item = layout->takeAt(0);
        if (!item) continue;
        QWidget *w = item->widget();
        if (w) {
            if (qobject_cast<CameraFeed*>(w))
                w->hide();          // Feed: gizle, silme
            else
                delete w;           // Placeholder: sil
        }
        delete item;
    }
    placeholders.clear();
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    comboBox = new QComboBox(central);
    mainLayout->addWidget(comboBox);

    gridWidget = new QWidget(central);
    gridLayout = new QGridLayout(gridWidget);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(4);
    mainLayout->addWidget(gridWidget, 1);

    tabWidget = new QTabWidget(central);
    tabWidget->setVisible(false);

    camChangeDebounce = new QTimer(this);
    camChangeDebounce->setSingleShot(true);
    camChangeDebounce->setInterval(500);
    connect(camChangeDebounce, &QTimer::timeout,
            this, &MainWindow::refreshLocalCameras);

    // Yüz motoru + araç çubuğu (feed'lerden ÖNCE kurulmalı: bağlantı için)
    setupFaceEngine();
    setupToolbar();

    // UDP feed'ler
    for (int port : CAMERA_PORTS) {
        CameraFeed *feed = new CameraFeed(port, gridWidget);
        feed->setFaceEngine(m_faceEngine, m_nextFeedId++);
        udpFeeds.append(feed);
        cameraFeeds.append(feed);

        connect(feed, &CameraFeed::firstFrameReceived, this, [feed]() {
            qDebug() << "[MainWindow] Pi bağlandı, port:" << feed->udpPort();
        });
    }

    mediaDevices = new QMediaDevices(this);
    connect(mediaDevices, &QMediaDevices::videoInputsChanged,
            this, &MainWindow::onCamerasChanged);

    refreshLocalCameras();

    connect(comboBox, &QComboBox::currentIndexChanged,
            this, &MainWindow::onCameraSelected);
}

// ─── Yüz Motoru (ayrı thread) ─────────────────────────────────────────────────
void MainWindow::setupFaceEngine()
{
    m_faceThread = new QThread(this);
    m_faceEngine = new FaceEngine(resolveModelsDir());   // parent YOK (moveToThread)
    m_faceEngine->moveToThread(m_faceThread);

    // Thread bitince motoru güvenle sil (Qt'nin önerdiği desen)
    connect(m_faceThread, &QThread::finished, m_faceEngine, &QObject::deleteLater);

    // Kişi listesini önbelleğe al (Yönet diyaloğu için)
    connect(m_faceEngine, &FaceEngine::personListReady, this,
            [this](const QStringList &names) { m_knownNames = names; });
    // DB değişince listeyi tazele
    connect(m_faceEngine, &FaceEngine::databaseChanged, this, [this](int) {
        QMetaObject::invokeMethod(m_faceEngine, "requestPersonList",
                                  Qt::QueuedConnection);
    });

    m_faceThread->start();

    // Başlangıç kişi listesini iste
    QMetaObject::invokeMethod(m_faceEngine, "requestPersonList",
                              Qt::QueuedConnection);
}

void MainWindow::setupToolbar()
{
    QToolBar *tb = addToolBar("Yüz Tanıma");
    tb->setMovable(false);

    m_faceToggleAct = tb->addAction("Yüz Tanıma: Kapalı");
    m_faceToggleAct->setCheckable(true);
    connect(m_faceToggleAct, &QAction::toggled, this, &MainWindow::onFaceToggled);

    QAction *enrollAct = tb->addAction("Kişi Ekle");
    connect(enrollAct, &QAction::triggered, this, &MainWindow::onEnrollClicked);

    QAction *manageAct = tb->addAction("Kişileri Yönet");
    connect(manageAct, &QAction::triggered, this, &MainWindow::onManageClicked);
}

void MainWindow::onFaceToggled(bool on)
{
    if (on && !m_faceEngine->isReady()) {
        QMessageBox::warning(this, "Yüz Tanıma",
            "Modeller yüklenemedi:\n\n" + m_faceEngine->lastError() +
            "\n\nModel dosyalarını 'models' klasörüne koyun.");
        m_faceToggleAct->setChecked(false);   // onFaceToggled tekrar tetiklenir (false)
        return;
    }
    m_faceToggleAct->setText(on ? "Yüz Tanıma: Açık" : "Yüz Tanıma: Kapalı");
    for (CameraFeed *feed : cameraFeeds)
        feed->setFaceRecognition(on);
}

void MainWindow::onEnrollClicked()
{
    if (!m_faceEngine->isReady()) {
        QMessageBox::warning(this, "Kişi Ekle",
            "Modeller yüklenemedi. Önce model dosyalarını ekleyin.");
        return;
    }
    EnrollDialog dlg(m_faceEngine, cameraFeeds, this);
    dlg.exec();
}

void MainWindow::onManageClicked()
{
    if (m_knownNames.isEmpty()) {
        QMessageBox::information(this, "Kişiler", "Kayıtlı kişi yok.");
        return;
    }
    bool ok = false;
    QString name = QInputDialog::getItem(this, "Kişileri Yönet",
        "Silinecek kişi:", m_knownNames, 0, false, &ok);
    if (ok && !name.isEmpty()) {
        if (QMessageBox::question(this, "Sil",
                QString("'%1' silinsin mi?").arg(name)) == QMessageBox::Yes) {
            QMetaObject::invokeMethod(m_faceEngine, "removePerson",
                                      Qt::QueuedConnection, Q_ARG(QString, name));
        }
    }
}

// ─── Yerel Kamera Yönetimi ────────────────────────────────────────────────────
void MainWindow::refreshLocalCameras()
{
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();

    QMap<QString, CameraFeed*> existing;
    for (CameraFeed *feed : localFeeds)
        existing[feed->cameraDeviceId()] = feed;

    QStringList newIds;
    for (const QCameraDevice &cam : cameras)
        newIds.append(QString::fromLatin1(cam.id()));

    // Bağlantısı kesilen kameraları kaldır
    for (auto it = existing.begin(); it != existing.end(); ++it) {
        if (!newIds.contains(it.key())) {
            CameraFeed *feed = it.value();
            localFeeds.removeAll(feed);
            feed->hide();
            feed->deleteLater();
            qDebug() << "[MainWindow] Kamera kaldırıldı:" << it.key();
        }
    }

    // Yeni kameralar
    for (const QCameraDevice &cam : cameras) {
        QString id = QString::fromLatin1(cam.id());
        if (!existing.contains(id)) {
            CameraFeed *feed = new CameraFeed(cam, gridWidget);
            feed->setFaceEngine(m_faceEngine, m_nextFeedId++);
            // Tanıma şu an açıksa yeni kamerada da aç
            if (m_faceToggleAct && m_faceToggleAct->isChecked())
                feed->setFaceRecognition(true);
            localFeeds.append(feed);
            qDebug() << "[MainWindow] Yeni kamera:" << cam.description();
        }
    }

    // Yerel kameralar önce, tüm UDP feed'ler her zaman dahil
    cameraFeeds.clear();
    cameraFeeds.append(localFeeds);
    cameraFeeds.append(udpFeeds);

    rebuildComboBox();
    updateGrid();
}

void MainWindow::onCamerasChanged()
{
    qDebug() << "[MainWindow] Kamera listesi değişti — debounce.";
    camChangeDebounce->start();
}

void MainWindow::rebuildComboBox()
{
    comboBox->blockSignals(true);
    comboBox->clear();
    comboBox->addItem("Tümü");

    for (CameraFeed *feed : localFeeds)
        comboBox->addItem(feed->cameraDescription());

    for (CameraFeed *feed : udpFeeds)
        comboBox->addItem(QString("Pi Kamera (Port %1)").arg(feed->udpPort()));

    comboBox->blockSignals(false);
}

// ─── Izgara Güncelleme ────────────────────────────────────────────────────────
void MainWindow::updateGrid()
{
    clearGridLayout(gridLayout, placeholders);

    for (int i = 0; i < 6; i++) {
        gridLayout->setRowStretch(i, 0);
        gridLayout->setColumnStretch(i, 0);
    }

    int count = cameraFeeds.size();
    if (count == 0) return;

    int cols, rows;
    if      (count == 1) { cols = 1; rows = 1; }
    else if (count == 2) { cols = 2; rows = 1; }
    else if (count <= 4) { cols = 2; rows = 2; }
    else if (count <= 6) { cols = 3; rows = 2; }
    else if (count <= 9) { cols = 3; rows = 3; }
    else                 { cols = 4; rows = (count + 3) / 4; }

    for (int r = 0; r < rows; r++) gridLayout->setRowStretch(r, 1);
    for (int c = 0; c < cols; c++) gridLayout->setColumnStretch(c, 1);

    qDebug() << "[updateGrid]" << count << "feed |" << cols << "x" << rows << "grid";
    for (int i = 0; i < rows * cols; i++) {
        int row = i / cols;
        int col = i % cols;

        if (i < count) {
            CameraFeed *feed = cameraFeeds[i];
            QString label = (feed->udpPort() == -1)
                                ? feed->cameraDescription()
                                : QString("UDP port %1").arg(feed->udpPort());
            qDebug() << "   slot" << i << "→ hücre(" << row << "," << col << "):" << label;

            feed->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            gridLayout->addWidget(feed, row, col);
            feed->show();
        } else {
            QWidget *ph = new QWidget(gridWidget);
            ph->setStyleSheet("background-color: #111;");
            gridLayout->addWidget(ph, row, col);
            placeholders.append(ph);
        }
    }
}

// ─── Görüntüleme Modları ──────────────────────────────────────────────────────
void MainWindow::showAll() { updateGrid(); }

void MainWindow::showSingle(int index)
{
    clearGridLayout(gridLayout, placeholders);

    for (int i = 0; i < 6; i++) {
        gridLayout->setRowStretch(i, 0);
        gridLayout->setColumnStretch(i, 0);
    }

    if (index >= 0 && index < cameraFeeds.size()) {
        gridLayout->setRowStretch(0, 1);
        gridLayout->setColumnStretch(0, 1);
        cameraFeeds[index]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        gridLayout->addWidget(cameraFeeds[index], 0, 0);
        cameraFeeds[index]->show();
    }
}

void MainWindow::onCameraSelected(int index)
{
    if (index == 0) showAll();
    else            showSingle(index - 1);
}

MainWindow::~MainWindow()
{
    // Worker thread'i düzgün durdur (motor finished→deleteLater ile silinir)
    if (m_faceThread) {
        m_faceThread->quit();
        m_faceThread->wait();
    }
    delete ui;
}
