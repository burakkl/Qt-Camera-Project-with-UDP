#ifndef FACEENGINE_H
#define FACEENGINE_H

#include <QObject>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QRect>
#include <QVector>
#include <QMetaType>

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>   // FaceDetectorYN, FaceRecognizerSF

// ─── Tek bir tanıma sonucu (bir yüz kutusu + kime ait olduğu) ────────────────
struct FaceResult {
    QRect   box;                 // yüzün görüntüdeki konumu (native çözünürlük)
    QString name;                // eşleşen kişi adı, yoksa "Bilinmiyor"
    float   confidence = 0.0f;   // kosinüs benzerliği (0..1, eşleşmede)
    bool    known = false;       // tanınan biri mi?
};
Q_DECLARE_METATYPE(FaceResult)

//
// FaceEngine — TÜM metodları worker thread'de çalışır (moveToThread + queued
// connection). Bu yüzden m_people'a sadece tek thread eriştiği için mutex'e
// gerek yoktur. Modeller bir kez (constructor'da) yüklenir.
//
class FaceEngine : public QObject
{
    Q_OBJECT
public:
    explicit FaceEngine(const QString &modelsDir, QObject *parent = nullptr);

    bool    isReady()   const { return m_ready; }
    QString lastError() const { return m_lastError; }

public slots:
    // Kameralardan gelen kare (queued connection ile çağrılır)
    void processFrame(int feedId, const QImage &frame);

    // Kişi kaydı: karedeki EN BÜYÜK yüzü bul, embedding'i isimle sakla
    void enrollFace(const QString &name, const QImage &frame);

    void removePerson(const QString &name);
    void loadDatabase();
    void saveDatabase();
    void requestPersonList();

signals:
    void resultsReady(int feedId, const QVector<FaceResult> &results);
    void enrollFinished(bool success, const QString &name, const QString &message);
    void personListReady(const QStringList &names);
    void databaseChanged(int personCount);

private:
    struct Person {
        QString name;
        cv::Mat feature;        // 1x128 CV_32F embedding
    };

    bool    initModels(const QString &modelsDir);
    bool    detectFaces(const cv::Mat &bgr, cv::Mat &facesOut);
    cv::Mat featureFor(const cv::Mat &bgr, const cv::Mat &faceRow);
    void    identify(const cv::Mat &feature, QString &nameOut,
                     float &scoreOut, bool &knownOut);

    static cv::Mat qimageToBgr(const QImage &img);

    cv::Ptr<cv::FaceDetectorYN>   m_detector;
    cv::Ptr<cv::FaceRecognizerSF> m_recognizer;

    QVector<Person> m_people;   // SADECE worker thread erişir → mutex gerekmez
    QString m_modelsDir;
    QString m_dbPath;
    bool    m_ready = false;
    QString m_lastError;

    // SFace kosinüs eşiği: >= eşik ise "aynı kişi" (OpenCV önerisi 0.363).
    // Kimlik doğrulamada güvenliği artırmak isterseniz 0.40-0.45'e çekin
    // (yanlış kabul azalır, ama tanıma biraz zorlaşır).
    static constexpr double COSINE_THRESHOLD = 0.5;
};

#endif // FACEENGINE_H
