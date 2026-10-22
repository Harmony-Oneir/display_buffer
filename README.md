# display_buffer




## Сборка
Для сборки используйте команду `./oneir/make.sh //oneir/display_buffer:display_buffer` в контейнере. Результат сборки лежит по пути `out/oneir/oneir/oneir_display/`:

| Библиотека | Описание |
|---|---|
| `libdisplay_buffer_vdi_impl.z.so` | Точка входа VDI аллокатора: экспортирует `CreateDisplayBufferVdi` / `DestroyDisplayBufferVdi`, реализует интерфейс `IDisplayBufferVdi` и загружается HDI-сервисом аллокатора через `dlopen` |
| `libdisplay_buffer_vendor.z.so` | Работа с памятью: выделение буферов через dumb buffers DRM (`hi_gbm`), mmap/unmap, экспорт в dma-buf и выбор DRM-устройства по подключённому коннектору (`oneir_drm_select`) |

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
