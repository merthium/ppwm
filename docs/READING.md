# Kodu okumaya başlarken

Önce `main.cpp`, ardından `WindowManager::run()` ile başla. `run()` iki işi koordine eder: X11 kuyruğundaki olayları işlemek ve barın periyodik çizim zamanını takip etmek. Arada `poll()` bekler. Arayüz 50 ms periyotla karşılaştırılır; değişmediyse X çizimi yapılmaz. Durum sağlayıcıları 2 saniyede bir okunur. Bar zamanlayıcısı için `steady_clock` kullanılır; sistem saati değişse bile periyot bozulmaz. Ekranda gösterilen saat ayrıca okunur.

Bir pencere açıldığında yol şu şekildedir:

```text
MapRequest → handle_event → manage_window → arrange → set_geometry
```

`Client`, yönetilen pencerenin durumudur. `Window` ise X sunucusunun verdiği sayısal kimliktir; bir C++ nesnesi veya sahip olunan pointer değildir. `clients_` vektörü Client nesnelerinin sahibidir. Eleman ekleyip silmek pointer'ları geçersiz kılabileceğinden, bir işlem dışına Client pointer'ı saklanmaz. Odak ve sürükleme durumu pencere kimliği tutar.

`layout.hpp` ile devam et. Bu dosyada X11 çağrısı yoktur: bir ekran alanı ve pencere sayısı alıp dikdörtgenler üretir. Yerleşim testleri gerçek X sunucusu olmadan çalışabilir. Tiled kutular kenarlıkları içerir; client boyutuna çevrilirken kenarlıklar düşülür. Floating geometri doğrudan client boyutudur.

Modern C++ özellikleri burada belirli sorunları çözmek için kullanılır:

- `X11Connection`, RAII ile bağlantının her çıkış yolunda kapanmasını sağlar. Kopyalanamaz; aynı bağlantının iki kez kapatılmasını önler.
- `std::unique_ptr`, bar ve tray nesnelerinin tek sahibini gösterir. Xlib'e verilen `Display*` bu sahiplikten ödünç alınan bir pointer'dır.
- `std::vector` değişken sayıdaki pencereleri, `std::array` sabit kayıt tablolarını tutar.
- `enum class Action`, tuş eylemlerini birbirine karışmayan isimlerle tanımlar.
- `constexpr` ayarların ve kısayol tablolarının derleme zamanında hazırlanmasını sağlar.
- `Rect::operator== = default`, geometri değişmediğinde X11'e yeniden taşıma isteği göndermemeyi kolaylaştırır.
- `std::chrono`, sürelerin birimini açık tutar; çıplak sayıları saniye/milisaniye diye tahmin etmek gerekmez.

Xlib bir C arayüzüdür. Özellik okurken dönen bellek `XFree` ile bırakılır. 32 bit X property alanları Xlib tarafında `long` dizileriyle taşınır; `reinterpret_cast` kullanılan dar noktalar bu protokol sınırıdır. Normal uygulama mantığına böyle dönüşümler taşınmaz.

Odak kodunda iki ayrıntı özellikle önemlidir. Gizlenen pencere de `UnmapNotify` üretir; bunu uygulamanın kapanmasıyla karıştırmamak için beklenen unmap sayısı tutulur. Pencereleri üst üste dizerken X sunucusu pointer giriş olayları üretebilir; kendi ürettiğimiz bu olaylar tekrar odak değiştirme döngüsüne sokulmaz.

Tray, WM'nin içinde ayrı bir küçük pencere yöneticisi gibidir. İkon başka bir uygulamanın penceresidir; `SystemTray` onu kendi konteynerine alır ve XEmbed mesajlarıyla haberleşir. Save-set, ppwm beklenmedik biçimde kapanırsa ikonun uygulamayla birlikte yok olmasını önler.

Bir değişiklik için önce en küçük ilgili dosyayı seç. Yeni yerleşim `layout.hpp`, yeni kısayol `config.hpp`, yeni durum sağlayıcısının gövdesi `bar_modules.cpp` içindedir. Çekirdek yalnızca pencere davranışı değişiyorsa değişmelidir.

Protokol ayrıntıları için [XEmbed](https://specifications.freedesktop.org/xembed/latest-single/) ve [System Tray](https://specifications.freedesktop.org/systemtray/latest-single/) standartları temel referanslardır. Kodun desteklemediği ayrıntılar README kapsam bölümünde belirtilir.
