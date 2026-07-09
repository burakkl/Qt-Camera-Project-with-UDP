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
#include <opencv2/objdetect.hpp>

struct FaceResult {
    QRect   box;
    QString name;
    float   confidence = 0.0f;
    bool    known = false;
};
Q_DECLARE_METATYPE(FaceResult)

class FaceEngine : public QObject
{
    Q_OBJECT
public:
    explicit FaceEngine(const QString &modelsDir, QObject *parent = nullptr);

    bool    isReady()   const { return m_ready; }
    QString lastError() const { return m_lastError; }

public slots:
    void processFrame(int feedId, const QImage &frame);

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
        cv::Mat feature;
    };

    bool    initModels(const QString &modelsDir);
    bool    detectFaces(const cv::Mat &bgr, cv::Mat &facesOut);
    cv::Mat featureFor(const cv::Mat &bgr, const cv::Mat &faceRow);
    void    identify(const cv::Mat &feature, QString &nameOut,
                     float &scoreOut, bool &knownOut);

    static cv::Mat qimageToBgr(const QImage &img);

    cv::Ptr<cv::FaceDetectorYN>   m_detector;
    cv::Ptr<cv::FaceRecognizerSF> m_recognizer;

    QVector<Person> m_people;
    QString m_modelsDir;
    QString m_dbPath;
    bool    m_ready = false;
    QString m_lastError;

    static constexpr double COSINE_THRESHOLD = 0.4;
};

#endif
