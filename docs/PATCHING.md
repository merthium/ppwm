# Kaynak patch iş akışı

ppwm kaynak üzerinden genişletilir. Yeni yerleşimler `layouts`, yeni bar sağlayıcıları `modules`, yeni kısayollar `keys` tablosuna eklenir. Kararlı bir ikili eklenti ABI'si yoktur.

Örnek eşit kolon patch'i:

```sh
git apply --check patches/0001-columns-layout.patch
git apply patches/0001-columns-layout.patch
cmake --build build -j
ctest --test-dir build --output-on-failure
```

WM yeniden başladıktan sonra `Alt+Ctrl+c` kolon yerleşimini seçer; `Alt+Space` son iki yerleşim arasında geçiş yapar. Geri almak için:

```sh
git apply -R patches/0001-columns-layout.patch
cmake --build build -j
```

Yerleşim imzası `std::vector<Rect>(Rect area, int count, double ratio, int gap, int masters)` şeklindedir. Sonuçlar kenarlık dahil kutulardır. Pencere sayısı sıfırken boş sonuç döndür. Normal ekranlarda sınırları ve çakışmayı test et. Tiled/geçici geometri için X11'e doğrudan erişme.

Bar sağlayıcısı `std::string(const BarState&)` döndürür; `BarModule` kaydı sağlayıcı ve isteğe bağlı tıklama komutunu tutar. Modüller ana iş parçacığında, periyodik olarak çalışır. Ağ erişimi, shell komutu ve uzun bekleme ekleme. Yavaş kaynaklar için ayrı veri üreticisi ve önbellek kullan.

Patch'ler kaynak bağlamına bağlıdır. Önce `--check` çalıştır; kendi değişikliklerini ayrı branch üzerinde tut. Değişiklikten önce bir baseline commit olması farkları yönetmeyi kolaylaştırır. Çekirdek veya tray değişikliklerinde `python3 scripts/nested-test.py` de çalıştırılmalıdır.
