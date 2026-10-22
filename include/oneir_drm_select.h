/*
 * oneir: выбор DRM-устройства для Display HDI.
 *
 * Вместо жёсткого "/dev/dri/card0" ищет первое устройство, у которого есть
 * подключённый коннектор (на гибридных ноутбуках card0 может оказаться
 * дискретной картой без выходов). Путь можно задать явно переменной
 * окружения ONEIR_DRM_DEVICE (например, /dev/dri/card1).
 */
#ifndef ONEIR_DRM_SELECT_H
#define ONEIR_DRM_SELECT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Путь к выбранному устройству. Выбор выполняется один раз на процесс. */
const char *OneirDrmDevicePath(void);

/* Открывает выбранное устройство (O_RDWR | O_CLOEXEC). Владение fd у вызывающего. */
int OneirOpenDrmDevice(void);

/*
 * Путь к файлу яркости подсветки: первый каталог в /sys/class/backlight
 * (intel_backlight, acpi_video0, ...). Явно: переменная ONEIR_BACKLIGHT_PATH.
 */
const char *OneirBacklightPath(void);

#ifdef __cplusplus
}
#endif

#endif /* ONEIR_DRM_SELECT_H */
