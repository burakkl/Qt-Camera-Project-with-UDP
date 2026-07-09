# Qt Camera Project with UDP

**Birkaç Raspberry Pi'yi yüz tanıyan bir gözetim sistemine dönüştürün.**

Pi kameralarını izlemek istediğiniz yerlere yerleştirin. Wi-Fi üzerinden görüntüyü merkezi bir Windows uygulamasına aktarırlar; uygulama tüm akışları tek bir canlı duvara birleştirir — aynı anda dört kameraya kadar. Uygulama hareketi kollar, bir şey olduğunda otomatik kayıt alır ve daha önce gördüğü yüzleri tanır.

Asıl güzeli sonra geliyor. Kaydı geri oynatırken, açılır menüden bir kişi seçin ve o kişinin kamerada göründüğü her ana anında atlayın. Sarma yok. Aramak yok. Bir isme tıklayın, ileriye basın, karşınızda.

---

## Ne yapıyor

🎥 **Canlı çoklu kamera duvarı.** Aynı Wi-Fi'daki dört Pi kamera, yan yana, gerçek zamanlı.

🧠 **Yüz tanıma.** Sisteme bir kez ekleyin, o andan itibaren bütün kameralar o kişiyi tanır. Yürüyüp geçerken isimler yüzlerin üstünde belirir.

🟢 **Akıllı kayıt.** Hareket olduğunda küçük bir yeşil nokta yanar. Kayıt kendi kendine başlar, olay sürerken devam eder, ortalık sakinleşince durur. Canınız istediğinde elle de kayıt başlatabilirsiniz.

⏪ **Yüz-atlamalı oynatma.** İşin bam teli bu. Bir kişi seçin, ileriye basın, o kişinin kadraja girdiği tam ana düşün. Her görünüm bir tık ötede.

🕒 **Her akışta canlı saat**, ki oynatmadaki zaman damgaları bir anlam ifade etsin.

---

## Ekran görüntüleri

> _Uygulama elinizde çalışırken buraya gerçek görüntüler koyun — grid görünümü, yüz tanıma iş başında, yüz-atlama çubuğuyla oynatma._

![Ana grid](docs/screenshots/main-grid.png)
![Yüz tanıma](docs/screenshots/face-recognition.png)
![Oynatma ve yüz-atlama](docs/screenshots/playback.png)

---

## Nasıl çalışıyor, kısaca

- Raspberry Pi kameraları videoyu encode edip Wi-Fi üzerinden Windows makinesine gönderiyor.
- Masaüstü uygulaması dört akışı da yakalayıp grid'de gösteriyor, o anda ekrandakiler üzerinde yüz tanıma çalıştırıyor.
- Bir şey hareket ettiğinde ilgili kamera kareleri diske yazmaya başlıyor. Ortalık sakinleşince kendi kendine duruyor.
- Tanınan her yüz zaman damgasıyla loglanıyor — yüz-atlama özelliğini mümkün kılan da bu.

**Qt 6** ve **OpenCV** (YuNet + SFace) ile yapıldı. Ağır iş arka planda ayrı bir thread'de dönüyor, o yüzden dört kamera da akıcı kalıyor.

---

## Hızlı başlangıç

Windows'ta Qt 6, OpenCV 4.8+ ve MSVC 2022 gerek. `qlabel_vers.pro`'yu Qt Creator'da açın, Release modunda derleyin, iki ONNX model dosyasını `C:/kamera_proje/models/` altına atın, çalıştırın.

Pi tarafında, kameradan bir kare alıp UDP paketi olarak 5000–5003 portlarından birine yollayan herhangi bir script iş görür — bir düzine satır Python yeter.

---

## Yeni bir yüz ekleme

Yüz tanımayı açın, **Enroll**'a tıklayın, kameranın önüne geçin, isim yazın. Tamam. O kişi o andan itibaren tüm kameralarda tanınır. **Manage** ile sonradan yeniden adlandırabilir veya silebilirsiniz.

---

## Hakkında

**Turkish Technic** stajım sırasında geliştirildi.

_Bu bir portföy / öğrenme projesi, ticari bir ürün değil._
