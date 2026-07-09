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
#include <QComboBox>
#include <QMap>
#include <QResizeEvent>

#include "faceengine.h"

struct FaceMarker {
    int     index;
    QString name;
};

class CameraFeed : public QWidget
{
    Q_OBJECT

public:
    explicit CameraFeed(int port, QWidget *parent = nullptr);

    explicit CameraFeed(const QCameraDevice &device, QWidget *parent = nullptr);

    ~CameraFeed();

    QString cameraDeviceId()    const;
    QString cameraDescription() const;

    int udpPort() const;

    void   setFaceEngine(FaceEngine *engine, int feedId);
    void   setFaceRecognition(bool on);
    QImage currentFrame() const { return m_lastFrame; }

signals:
    void firstFrameReceived();

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

    void onFaceResults(int feedId, const QVector<FaceResult> &results);

    void onRecordButtonToggled(bool on);
    void recordWatchdogTick();

    void onPrevFace();
    void onNextFace();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class Mode { UDP, Local };
    Mode mode;

    QLabel      *liveLabel;
    QPushButton *watchBtn;
    QPushButton *liveBtn;
    QPushButton *pauseBtn;
    QSlider     *slider;
    QTimeEdit   *timeEdit;

    QUdpSocket *udpSocket;
    int         port;
    QSet<QString> seenSenders;

    QCamera              *camera;
    QMediaCaptureSession *session;
    QVideoSink           *videoSink;

    QTimer          *segmentTimer;
    QQueue<QString>  segmentFiles;
    QString          currentSegDir;
    int              frameCount;
    static const int MAX_SEGMENTS = 6;

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

    FaceEngine          *m_engine      = nullptr;
    int                  m_feedId      = -1;
    bool                 m_faceEnabled = false;
    bool                 m_faceBusy    = false;
    QElapsedTimer        m_submitTimer;
    QVector<FaceResult>  m_latestResults;
    QImage               m_lastFrame;
    static const int     MIN_SUBMIT_INTERVAL_MS = 200;

    void renderFrame(const QImage &img);
    void maybeSubmit(const QImage &img);

    QLabel       *motionDot     = nullptr;
    QPushButton  *recordBtn     = nullptr;

    QImage        m_prevGray;
    QElapsedTimer m_motionSampleTimer;
    QElapsedTimer m_lastMotionTimer;
    int           m_motionState  = -1;
    bool          m_recording    = false;
    bool          m_manualRecord = false;
    QTimer       *recordWatchdog = nullptr;
    QLabel       *m_recBadge     = nullptr;

    bool detectMotionInFrame(const QImage &liveImg);
    void updateMotion(const QImage &liveImg);
    void setMotionIndicator(bool active);
    void startRecording();
    void stopRecording();
    void updateRecordButton();

    static const int        MOTION_W           = 160;
    static const int        MOTION_H           = 120;
    static constexpr int    MOTION_SAMPLE_MS   = 120;
    static constexpr int    MOTION_PIXEL_DELTA = 25;
    static constexpr double MOTION_AREA_FRAC   = 0.010;
    static constexpr int    MOTION_HOLD_MS     = 700;
    static constexpr int    RECORD_LINGER_MS   = 60000;

    QList<FaceMarker> m_faceMarkers;
    QMap<QString,int> m_lastSeenFrame;

    QWidget     *faceBar        = nullptr;
    QComboBox   *m_personCombo  = nullptr;
    QPushButton *m_prevFaceBtn  = nullptr;
    QPushButton *m_nextFaceBtn  = nullptr;
    QLabel      *m_faceJumpInfo = nullptr;

    QLabel      *m_clockLabel   = nullptr;
    QTimer      *m_clockTimer   = nullptr;

    void logFaceAppearances();
    void appendFaceLog(int frameNo, const QString &name);
    void populateFaceJump();
    void jumpToFace(int dir);
    void seekTo(int index);

    static constexpr int FACE_REAPPEAR_GAP_FRAMES = 45;
};

#endif