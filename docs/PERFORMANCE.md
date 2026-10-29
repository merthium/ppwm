# Performans

Amaç, basit ve okunabilir kodla düşük boşta maliyet ve hızlı pencere işlemleridir. Ölçümler donanım, güç profili, X sunucusu ve masaüstü yüküyle değişir.

## Ölçüm

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
python3 scripts/nested-test.py
```

Betik ayrı bir 1024×768 Xephyr sunucusu açar. Fonksiyonel testlerden sonra 64 pencere oluşturur, 10.000 floating geometri isteği gönderir, bir etiket mesajıyla işlemlerin tamamlanmasını bekler ve pencereleri kapatır. `/proc` üzerinden yalnızca ppwm sürecinin CPU süresini ve RSS değerini okur.

Örnek koşu (2026-10-28): x86-64, Intel Core i5-1145G7, GCC 14.2.1, Release + LTO.

| Ölçüm | Sonuç |
| --- | --- |
| 64 pencereyi yönetmeye alma | 290,5 ms |
| 10.000 configure isteği + etiket bariyeri | 2.151,6 ms |
| Geri gösterme ve pencereleri kaldırma | 29,1 ms |
| Yük testinde ppwm CPU süresi | 0,25 s |
| Yük sonrası RSS | 14.036 KiB |
| 3 saniye boşta CPU sayacı artışı | 0 tick |

CPU sayacı çözünürlüğü sınırlıdır; 0 tick, CPU'nun hiç kullanılmadığı anlamına gelmez. RSS paylaşılan kütüphane sayfalarını da içerir. Bu değerler Xephyr, compositor ve istemci uygulamalarının CPU/bellek maliyetlerini içermez. Süreler tek koşudan alınan örneklerdir, gecikme garantisi veya p95/p99 ölçümü değildir.

## Uygulanan seçimler

- Boşta `poll()`; sürekli dönen kontrol döngüsü yok.
- Bar zamanlaması `steady_clock` ile, varsayılan 2 saniye.
- X olayları sınırlı gruplar halinde işlenir; yoğunluk bar zamanlayıcısını sınırsız ertelemez.
- Sıradaki ardışık fare hareketleri son konuma birleştirilir. ButtonRelease gibi farklı olayların üzerinden atlanmaz.
- Geometri aynıysa yeniden `XMoveResizeWindow` gönderilmez. ConfigureRequest için gerekli protokol yanıtı yine verilir.
- Floating configure isteği yalnızca ilgili pencereyi günceller; bütün yerleşimi tekrar hesaplamaz.
- Yerleşimde geçici Client pointer'ları kullanılır; her kutu için vektör yeniden aranmaz.
- Yerleşim vektörleri ihtiyaç duydukları kapasiteyi önceden ayırır.
- Bar arka pixmap üzerine çizilir ve tek kopyayla gösterilir.
- Pencere başlıkları property değiştiğinde önbelleğe alınır; her bar çiziminde tekrar X sorgusu yapılmaz.
- Pil/keep-awake verileri yerel dosyalardan okunur; periyodik shell süreçleri yok.

X11 başlangıcı, odak, protokol özellikleri ve tray işlemlerinde gerekli senkronizasyonlar korunur. Daha ileri optimizasyon için önce gerçek iş yükünü profile et; C++ kodunun kısalığını veya karmaşıklığını performans göstergesi sayma.

## Ham ölçüm kaydı

Son Release derlemesinde `python3 scripts/nested-test.py` çıktısı:

```text
X11 integration passed
Terminal swallowing integration passed
Bar tabs, multitag and per-tag integration passed
Tray integration passed
64 windows: map 290.472 ms; 10000 configure requests + tag barrier: 2151.62 ms; restore/destroy 29.0969 ms
Stress WM CPU seconds: 0.250
Idle CPU seconds / 3 wall seconds: 0.000; VmRSS: 14036 kB
Restart executes replacement binary.
Nested integration and second-WM rejection passed.
```

Ayrıca process, workspace ve layout birim testlerinin üçü geçti. Test kaydı hız karşılaştırması değildir; başka bir WM ile aynı iş yükünde karşılaştırma henüz yapılmadı.

## Odak ve bar düzeltmeleri sonrası (2026-10-29)

Bu koşuda Xephyr, eşlenmemiş bir üst pencere içinde çalıştırıldı. Ana masaüstünün fiziksel fare olayları otomatik odak testlerine karışmaz; bu koşu görünür masaüstünün çizim/compositor performansını ölçmez. Önceki görünür Xephyr koşusuyla doğrudan hız kıyası yapılmamalıdır.

```text
Root/bar cursor integration passed
X11 integration passed
Terminal swallowing integration passed
Bar tabs, multitag and per-tag integration passed
Tray integration passed
64 windows: map 82.1007 ms; 10000 configure requests + tag barrier: 315.681 ms; restore/destroy 8.927 ms
Stress WM CPU seconds: 0.050
Idle CPU seconds / 3 wall seconds: 0.000; VmRSS: 15480 kB
Restart executes replacement binary.
Nested integration and second-WM rejection passed.
```

Ek regresyonlar: 12 ardışık etiket gidiş/dönüşünde son odağın korunması, aynı etikette gizli pencerenin etkinleştirilmesi, sabit imleç altındaki pencereye tıklayarak odaklanma ve tıklamanın uygulamaya iletilmesi, kök/bar imlecinin XFixes görüntüsüyle doğrulanması. Üç birim testi de geçti.
