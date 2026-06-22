#include "faceengine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

#include <opencv2/imgproc.hpp>

// ─────────────────────────────────────────────────────────────────────────────
FaceEngine::FaceEngine(const QString &modelsDir, QObject *parent)
    : QObject(parent)
    , m_modelsDir(modelsDir)
{
    // Queued connection'da custom tipleri taşıyabilmek için kayıt
    qRegisterMetaType<FaceResult>("FaceResult");
    qRegisterMetaType<QVector<FaceResult>>("QVector<FaceResult>");

    // Veritabanı, mevcut kayıt klasörünüzle aynı yerde
    m_dbPath = "C:/kamera_proje/faces.json";

    m_ready = initModels(modelsDir);
    if (m_ready)
        loadDatabase();
}

bool FaceEngine::initModels(const QString &modelsDir)
{
    const QString detPath =
        QDir(modelsDir).filePath("face_detection_yunet_2023mar.onnx");
    const QString recPath =
        QDir(modelsDir).filePath("face_recognition_sface_2021dec.onnx");

    if (!QFile::exists(detPath) || !QFile::exists(recPath)) {
        m_lastError = QString("Model dosyaları bulunamadı:\n%1\n%2")
                          .arg(detPath, recPath);
        qWarning() << "[FaceEngine]" << m_lastError;
        return false;
    }

    try {
        // (model, config, inputSize, scoreThr, nmsThr, topK)  — OpenCV demo ile aynı
        m_detector = cv::FaceDetectorYN::create(
            detPath.toStdString(), "", cv::Size(320, 320),
            0.9f, 0.3f, 5000);

        m_recognizer = cv::FaceRecognizerSF::create(
            recPath.toStdString(), "");
    } catch (const cv::Exception &e) {
        m_lastError = QString("OpenCV model yükleme hatası: %1").arg(e.what());
        qWarning() << "[FaceEngine]" << m_lastError;
        return false;
    }

    qDebug() << "[FaceEngine] Modeller yüklendi:" << modelsDir;
    return true;
}

// ─── QImage (RGB) → cv::Mat (BGR) ────────────────────────────────────────────
cv::Mat FaceEngine::qimageToBgr(const QImage &img)
{
    QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    // cv::Mat başlığı QImage tamponuna işaret eder; cvtColor YENİ tampon ayırır.
    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC3,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
    return bgr;
}

bool FaceEngine::detectFaces(const cv::Mat &bgr, cv::Mat &facesOut)
{
    if (bgr.empty()) return false;
    try {
        m_detector->setInputSize(bgr.size());
        m_detector->detect(bgr, facesOut);
    } catch (const cv::Exception &e) {
        qWarning() << "[FaceEngine] detect hatası:" << e.what();
        return false;
    }
    return true;
}

cv::Mat FaceEngine::featureFor(const cv::Mat &bgr, const cv::Mat &faceRow)
{
    cv::Mat aligned, feature;
    try {
        m_recognizer->alignCrop(bgr, faceRow, aligned);
        m_recognizer->feature(aligned, feature);
    } catch (const cv::Exception &e) {
        qWarning() << "[FaceEngine] feature hatası:" << e.what();
        return cv::Mat();
    }
    // feature() iç tamponu yeniden kullanır → KLONLAMAK ŞART
    return feature.clone();
}

void FaceEngine::identify(const cv::Mat &feature, QString &nameOut,
                          float &scoreOut, bool &knownOut)
{
    nameOut  = QStringLiteral("Bilinmiyor");
    scoreOut = 0.0f;
    knownOut = false;
    if (feature.empty()) return;

    double bestScore = -1.0;
    int    bestIdx   = -1;
    for (int i = 0; i < m_people.size(); ++i) {
        double s = m_recognizer->match(feature, m_people[i].feature,
                                       cv::FaceRecognizerSF::FR_COSINE);
        if (s > bestScore) { bestScore = s; bestIdx = i; }
    }

    scoreOut = static_cast<float>(bestScore);
    if (bestIdx >= 0 && bestScore >= COSINE_THRESHOLD) {
        nameOut  = m_people[bestIdx].name;
        knownOut = true;
    }
}

// ─── Canlı tanıma ────────────────────────────────────────────────────────────
void FaceEngine::processFrame(int feedId, const QImage &frame)
{
    QVector<FaceResult> results;
    if (!m_ready) { emit resultsReady(feedId, results); return; }

    cv::Mat bgr = qimageToBgr(frame);
    cv::Mat faces;

    if (detectFaces(bgr, faces) && !faces.empty()) {
        for (int i = 0; i < faces.rows; ++i) {
            cv::Mat row = faces.row(i);
            FaceResult fr;
            fr.box = QRect(static_cast<int>(row.at<float>(0, 0)),
                           static_cast<int>(row.at<float>(0, 1)),
                           static_cast<int>(row.at<float>(0, 2)),
                           static_cast<int>(row.at<float>(0, 3)));

            cv::Mat feat = featureFor(bgr, row);
            identify(feat, fr.name, fr.confidence, fr.known);
            results.append(fr);
        }
    }

    emit resultsReady(feedId, results);
}

// ─── Kişi kaydı ──────────────────────────────────────────────────────────────
void FaceEngine::enrollFace(const QString &name, const QImage &frame)
{
    if (!m_ready) {
        emit enrollFinished(false, name, "Yüz motoru hazır değil (modeller yüklenemedi).");
        return;
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        emit enrollFinished(false, name, "İsim boş olamaz.");
        return;
    }

    cv::Mat bgr = qimageToBgr(frame);
    cv::Mat faces;
    if (!detectFaces(bgr, faces) || faces.empty()) {
        emit enrollFinished(false, trimmed, "Karede yüz bulunamadı. Kameraya net bakın.");
        return;
    }

    // En büyük yüzü seç (w*h)
    int   bestIdx  = 0;
    float bestArea = 0.0f;
    for (int i = 0; i < faces.rows; ++i) {
        float a = faces.at<float>(i, 2) * faces.at<float>(i, 3);
        if (a > bestArea) { bestArea = a; bestIdx = i; }
    }

    cv::Mat feat = featureFor(bgr, faces.row(bestIdx));
    if (feat.empty()) {
        emit enrollFinished(false, trimmed, "Yüz özniteliği çıkarılamadı.");
        return;
    }

    // Aynı isim varsa güncelle, yoksa ekle
    bool updated = false;
    for (auto &p : m_people) {
        if (p.name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            p.feature = feat;
            updated   = true;
            break;
        }
    }
    if (!updated)
        m_people.append({ trimmed, feat });

    saveDatabase();
    emit databaseChanged(m_people.size());
    emit enrollFinished(true, trimmed,
        updated ? QString("%1 güncellendi.").arg(trimmed)
                : QString("%1 kaydedildi.").arg(trimmed));
}

void FaceEngine::removePerson(const QString &name)
{
    for (int i = 0; i < m_people.size(); ++i) {
        if (m_people[i].name.compare(name, Qt::CaseInsensitive) == 0) {
            m_people.removeAt(i);
            saveDatabase();
            emit databaseChanged(m_people.size());
            return;
        }
    }
}

// ─── Disk: JSON ──────────────────────────────────────────────────────────────
void FaceEngine::saveDatabase()
{
    QJsonArray arr;
    for (const auto &p : m_people) {
        QJsonArray feat;
        const float *data = p.feature.ptr<float>(0);
        for (int i = 0; i < p.feature.cols; ++i)
            feat.append(static_cast<double>(data[i]));

        QJsonObject obj;
        obj["name"]    = p.name;
        obj["feature"] = feat;
        arr.append(obj);
    }

    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());
    QFile f(m_dbPath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[FaceEngine] DB yazılamadı:" << m_dbPath;
        return;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    qDebug() << "[FaceEngine] DB kaydedildi:" << m_people.size() << "kişi";
}

void FaceEngine::loadDatabase()
{
    m_people.clear();
    QFile f(m_dbPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "[FaceEngine] DB yok, boş başlanıyor.";
        emit databaseChanged(0);
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) { emit databaseChanged(0); return; }

    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        const QJsonObject obj  = v.toObject();
        const QString     name = obj["name"].toString();
        const QJsonArray  feat = obj["feature"].toArray();
        if (name.isEmpty() || feat.isEmpty()) continue;

        cv::Mat m(1, feat.size(), CV_32F);
        float *data = m.ptr<float>(0);
        for (int i = 0; i < feat.size(); ++i)
            data[i] = static_cast<float>(feat[i].toDouble());

        m_people.append({ name, m });
    }
    qDebug() << "[FaceEngine] DB yüklendi:" << m_people.size() << "kişi";
    emit databaseChanged(m_people.size());
}

void FaceEngine::requestPersonList()
{
    QStringList names;
    for (const auto &p : m_people) names << p.name;
    emit personListReady(names);
}
