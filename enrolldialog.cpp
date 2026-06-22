#include "enrolldialog.h"
#include "faceengine.h"
#include "camerafeed.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QPixmap>

EnrollDialog::EnrollDialog(FaceEngine *engine,
                           const QList<CameraFeed*> &feeds,
                           QWidget *parent)
    : QDialog(parent)
    , m_engine(engine)
    , m_feeds(feeds)
{
    setWindowTitle("Kişi Ekle / Kaydet");
    setMinimumSize(480, 440);

    QVBoxLayout *lay = new QVBoxLayout(this);

    // Hangi kameradan kaydedeceğiz?
    m_camSelect = new QComboBox(this);
    for (CameraFeed *f : m_feeds) {
        QString label = (f->udpPort() == -1)
            ? f->cameraDescription()
            : QString("Pi Kamera (Port %1)").arg(f->udpPort());
        m_camSelect->addItem(label);
    }
    lay->addWidget(m_camSelect);

    // Canlı önizleme
    m_preview = new QLabel(this);
    m_preview->setMinimumSize(440, 300);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setScaledContents(true);
    m_preview->setStyleSheet("background:#000; color:#777;");
    m_preview->setText("Önizleme bekleniyor...");
    lay->addWidget(m_preview, 1);

    // İsim + kaydet
    QHBoxLayout *row = new QHBoxLayout();
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("Kişinin adı");
    m_captureBtn = new QPushButton("Yüzü Kaydet", this);
    row->addWidget(m_nameEdit, 1);
    row->addWidget(m_captureBtn);
    lay->addLayout(row);

    m_status = new QLabel(this);
    m_status->setStyleSheet("color:#888;");
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    connect(m_captureBtn, &QPushButton::clicked, this, &EnrollDialog::onCapture);
    connect(m_engine, &FaceEngine::enrollFinished,
            this, &EnrollDialog::onEnrollFinished);

    m_previewTimer = new QTimer(this);
    connect(m_previewTimer, &QTimer::timeout, this, &EnrollDialog::updatePreview);
    m_previewTimer->start(100);   // ~10 fps önizleme
}

QImage EnrollDialog::currentSelectedFrame() const
{
    const int idx = m_camSelect->currentIndex();
    if (idx < 0 || idx >= m_feeds.size()) return {};
    return m_feeds[idx]->currentFrame();   // overlay'siz ham kare
}

void EnrollDialog::updatePreview()
{
    QImage img = currentSelectedFrame();
    if (!img.isNull())
        m_preview->setPixmap(QPixmap::fromImage(img));
}

void EnrollDialog::onCapture()
{
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) { m_status->setText("Lütfen bir isim girin."); return; }

    QImage img = currentSelectedFrame();
    if (img.isNull()) { m_status->setText("Kameradan görüntü alınamadı."); return; }

    m_status->setStyleSheet("color:#888;");
    m_status->setText("İşleniyor...");
    m_captureBtn->setEnabled(false);

    // Worker thread'e gönder
    QMetaObject::invokeMethod(m_engine, "enrollFace", Qt::QueuedConnection,
                              Q_ARG(QString, name),
                              Q_ARG(QImage, img));
}

void EnrollDialog::onEnrollFinished(bool ok, const QString &name, const QString &msg)
{
    Q_UNUSED(name)
    m_captureBtn->setEnabled(true);
    m_status->setStyleSheet(ok ? "color:#2ca85a;" : "color:#e04646;");
    m_status->setText(msg);
    if (ok) m_nameEdit->clear();
}
