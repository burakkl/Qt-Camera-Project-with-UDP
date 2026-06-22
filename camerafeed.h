#ifndef CAMERAFEED_H
#define CAMERAFEED_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimeEdit>
#include <QUdpSocket>
#include <QTimer>
#include <QQueue>
#include <QSet>
#include <QVector>
#include <QElapsedTimer>
#include <QImage>
#include <QNetworkDatagram>
#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoFrameFormat>

#include "faceengine.h"   // FaceResult + FaceEngine

class CameraFeed : public QWidget
{
    Q_OBJECT

public:
    // UDP kamera: Pi'den gelen görüntü
    explicit CameraFeed(int port, QWidget *parent = nullptr);

    // Yerel kamera: belirli bir cihaz
    explicit CameraFeed(const QCameraDevice &device, QWidget *parent = nullptr);

    ~CameraFeed();

    // MainWindow'un kamera takibi için
    QString cameraDeviceId()    const;
    QString cameraDescription() const;

    // UDP modu için port numarası (-1 = yerel kamera)
    int udpPort() const;

    // ── Yüz tanıma ──────────────────────────────────────────────────────────
    void   setFaceEngine(FaceEngine *engine, int feedId);
    void   setFaceRecognition(bool on);
    QImage currentFrame() const { return m_lastFrame; }   // overlay'siz ham kare

signals:
    void firstFrameReceived();  // UDP: ilk datagram gelince

private slots:
    void readData();
    void onLocalFrame(const QVideoFrame &frame);
    void newSegment();
    void onWatchClicked();
    void onLiveClicked();
    void onPauseClicked();
    void onSliderChanged(int value);
    void onTimeChanged(const QTime &time);
    void nextPlaybackFrame();

    // Yüz motorundan sonuç geldiğinde
    void onFaceResults(int feedId, const QVector<FaceResult> &results);

private:
    enum class Mode { UDP, Local };
    Mode mode;

    // UI
    QLabel      *liveLabel;
    QPushButton *watchBtn;
    QPushButton *liveBtn;
    QPushButton *pauseBtn;
    QSlider     *slider;
    QTimeEdit   *timeEdit;

    // UDP (sadece UDP modunda)
    QUdpSocket *udpSocket;
    int         port;
    QSet<QString> seenSenders;

    // Yerel kamera (sadece Local modunda)
    QCamera              *camera;
    QMediaCaptureSession *session;
    QVideoSink           *videoSink;

    // Kayıt
    QTimer          *segmentTimer;
    QQueue<QString>  segmentFiles;
    QString          currentSegDir;
    int              frameCount;
    static const int MAX_SEGMENTS = 6;

    // Oynatma
    QTimer        *playbackTimer;
    QList<QString> playbackFrames;
    int            playbackIndex;
    bool           isPlayback;
    bool           isPaused;
    bool           wasEverActive;

    void setupUI();
    void setupTimers();
    void setupSignals();

    void saveFrame(const QByteArray &data);
    void startNewSegment();
    void collectPlaybackFrames();
    void showFrame(int index);
    void goToPlayback();
    void goToLive();

    // ── Yüz tanıma durumu (in-class init → constructor'lar değişmez) ─────────
    FaceEngine          *m_engine      = nullptr;
    int                  m_feedId      = -1;
    bool                 m_faceEnabled = false;
    bool                 m_faceBusy    = false;   // tek seferde tek kare yolla
    QElapsedTimer        m_submitTimer;
    QVector<FaceResult>  m_latestResults;
    QImage               m_lastFrame;
    static const int     MIN_SUBMIT_INTERVAL_MS = 200;  // ~5 kare/sn tanıma

    void renderFrame(const QImage &img);   // overlay çizip ekrana basar
    void maybeSubmit(const QImage &img);   // motora kare gönderir (kısıtlı)
};

#endif // CAMERAFEED_H
