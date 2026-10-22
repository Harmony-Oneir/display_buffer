# display_buffer




## Сборка
Для сборки используйте команду `./oneir/make.sh //oneir/display_buffer:display_buffer` в контейнере. Результат сборки лежит по пути `out/oneir/oneir/oneir_display/`:

| Библиотека | Описание |
|---|---|
| `libdisplay_buffer_vdi_impl.z.so` | Точка входа VDI аллокатора: экспортирует `CreateDisplayBufferVdi` / `DestroyDisplayBufferVdi`, реализует интерфейс `IDisplayBufferVdi` и загружается HDI-сервисом аллокатора через `dlopen` |
| `libdisplay_buffer_vendor.z.so` | Работа с памятью: выделение буферов через dumb buffers DRM (`hi_gbm`), mmap/unmap, экспорт в dma-buf и выбор DRM-устройства по подключённому коннектору (`oneir_drm_select`) |

## Сборка тестов
Для сборки тестов используйте команду `./make.sh --reload display_buffer_test` в контейнере. Результат сборки:

| Тест | Описание |
|---|---|
| `hdf_disp_buffer_unittest` | Функциональный unit-тест Display Buffer HDI из `drivers/peripheral/display/buffer/test/unittest` (58 тестов на gtest). Проверяет корректность работы аллокатора: выделение буферов разных размеров, форматов и флагов usage (`AllocMem`), отображение в память (`Mmap`/`Unmap`), синхронизацию кэшей, повторное выделение (`ReAllocMem`), passthrough-режим и клонирование dma-buf (`CloneDmaBufferHandle`), а также негативные сценарии с неподдерживаемыми параметрами. Результат — прошёл/не прошёл по каждому случаю |
| `display_buffer_perf` | Тест производительности из репозитория `display_buffer` (`tests/perf`). Работает как обычный клиент HDI и для 720p, 1080p и 4K (`RGBA_8888`) измеряет задержки `AllocMem`+`FreeMem`, `Mmap`+`Unmap`, `FlushCache` и `InvalidateCache` (avg, p50, p95, max), скорость записи и чтения CPU через отображённую память (ГБ/с) и кадровый цикл из трёх буферов (map → заполнение → flush → unmap) с оценкой кадров в секунду. Флаг `-n` задаёт число итераций, `--strict` возвращает код 1 при превышении порогов |

## Тестирование на стенде
После запуска `ohos-allocator-host.service` проверьте загружаемые библиотеки:
```bash
P=$(pidof allocator_host); echo "PID=$P"
sudo grep -oE '/[^ ]+\.so' /proc/$P/maps | sort -u | grep -E "allocator|composer|vdi_impl|display_buffer_vendor"
```
Должны загружаться следующие библиотеки вместо `_default`:

```bash
/vendor/lib64/liballocator_driver_1.0.z.so
/vendor/lib64/liballocator_service_1.0.z.so
/vendor/lib64/liballocator_service_1.4.z.so
/vendor/lib64/libdisplay_buffer_vdi_impl.z.so
/vendor/lib64/libdisplay_buffer_vendor.z.so
```
