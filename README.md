# Файловый интерфейс к мессендежру

**Note.** Для загрузки модуля очевидно нужно ядро линукса. Далее предполагаю, что у пользователя оно установлено и собрано. Назовем директорию, в которой расположено ядро `KDIR`.

## Сборка

### 1. Подготовка

1. Устанавливаем busybox. Собираем его статически.

2. Создаем rootfs: директория `KDIR/root/`. В `KDIR/root/bin` должны находится все утилиты из busybox.

3. Создаем исполняемый файл `KDIR/root/init`  с содержимым:

```
#!/bin/sh

mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

mount -t tmpfs none /tmp

exec setsid sh -c 'exec sh </dev/ttyS0 >/dev/ttyS0 2>&1'
```

4. Создаем директории `/proc`, `/dev`, `/sys` в `KDIR/root`.

5. Создаем отдельную папку модуля, рядом с ядром, назовем ее `KDIR/tg_module`.

6. В эту папку кладем файлы из репозитория: `Makefile`, `messsenger.py`, `tg_dev.c`. **Важно:** в Makefile переменную DIR нужно изменить: указать свой путь до `KDIR/root`, а также свою версию ядра.

7. Собираем наш модуль: вызываем `make` из директории `KDIR/tg_module`.

8. Полученный файл `tg_dev.ko` копируем в `KDIR/root/`.

### 2. Компиляция messenger.py

В нашем ядре нет python3, который мог облегчить нам жизнь и запустить messenger.py. Чтобы не устанавливать зависимости для питона в ядро, мы лучше скомпилируем messenger.py.

1. На хосте создаем venv (и активируем):

`python3 -m venv .venv`

`source .venv/bin/activate`

2. Устанавливаем Nuitka для (почти) статической компиляции python файлов.

`python -m pip install Nuitka`

3. Компилируем наш файл `messenger.py`:

`nuitka   --standalone   --onefile   --static-libpython=yes   --enable-plugin=no-qt   --noinclude-default-mode=warning   messenger.py`

4. В рабочей директории должна появиться папка `messenger.dist`. Мы ее полностью копируем в `KDIR/root`: 

`cp -r messenger.dist KDIR/root/`

5. Питон очень сложно скомпилировать статически в один бинарный файл, так чтобы все сразу работало. Поэтому, несмотря на использование специальной утилиты nuitka, бинарь `messenger.dist/messenger.bin` все еще содержит зависимости:

`ldd messenger.dist/messenger.bin`

В моем случае он ссылается на

- `/lib/x86_64-linux-gnu/libz.so.1`

- `/lib/x86_64-linux-gnu/libm.so.6`

- `/lib/x86_64-linux-gnu/libc.so.6`

- `/lib64/ld-linux-x86-64.so.2`

Все эти библиотеки мы должны скопировать в `KDIR/root/`, сохранив их пути (т.е. нужно также создать директории `KDIR/root/lib/x86_64-linux-gnu`, `KDIR/root/lib64/` и положить туда соответствующие библиотеки).

### 3. Запуск

1. Идем в `KDIR/root/`. Выполняем скрипт, который позволит ядру увидеть наш root.

`find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../boot/initramfs.gz`

2. Идем в `KDIR/boot/`. Так как я предполагаю, что ядро собрано, то ожидаю, что в этой папку уже лежит собранное ядро линукс вместе с конфигурациями. В моем случае это файл `vmlinuz-6.18.8`.

3. Запускаем виртуалку:

`qemu-system-x86_64 -kernel ./vmlinuz-6.18.8 -initrd initramfs.gz -append console=ttyS0 -nographic -m 1024`

**Note.** Если вдруг возникает kernel panick, попробуйте увеличить размер оперативки до 2048 (-m 2048)

## Архитектура

1. Программа `messenger.py` эмулирует настоящий мессенджер. Она работает в фоновом режиме и обрабатывает запросы от kernel space.

Протокол взаимодействия следующий:

kernel spase -> user space: 

- `write <chat_id> <msg>`
- `read <chat_id>`

user space -> kernel space:

- При ошибке: `ERROR: <txt>`
- Иначе: `<txt>`

2. Взаимодействие kernel space и user space происходит через character devices, а также fifo.

Чаты мессенджера соответствуют файлам `/dev/telegram/chat_<id>`.

Файловые операции интерпретируются следующим образом:

| Операция | Значение |
|-------------|-------------|
| open()   | Открыть чат   |
| read()   | Получить сообщения   |
| write()   | Отправить сообщение    |
| close()   | Закрыть чат   |

3. Собственно, сам модуль `tg_dev.c`.

Модуль создает файловый интерфейс для чатов, реализует для них операции, передает запросы в user space (и получает ответы) с помощью fifo.