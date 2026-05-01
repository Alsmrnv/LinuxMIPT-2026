# Системный вызов, преобразующий виртуальный адрес в физический

## Сборка и запуск

*Note* Первые 4 пункта необязательны, ядро уже собрано, можно сразу переходить к пункту *Запуск*. 

1. Клонируем репозиторий и переходим на нужную ветку

```
git clone https://github.com/Alsmrnv/LinuxMIPT-2026.git
git switch syscall_va_pa
```

2. Собираем ядро

```
cd linux-6.18.8/
make -j8
```

3. Устанавливаем образ ядра

```
INSTALL_PATH=../boot/ make install
```

4. Собираем initramfs

```
cd root/
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../boot/initramfs.gz
```

5. Запуск ядра

```
qemu-system-x86_64 -kernel ./vmlinuz-6.18.8 -initrd initramfs.gz -append console=ttyS0 -nographic -m 1024
```

6. Проверяем работу системного вызова

```
# ./get_addr_program
```

## Проделанная работа

1. Информация о системном вызове добавлена в `linux-6.18.8/arch/x86/entry/syscalls/syscall_64.tbl`

2. Реализован системный вызов в файле `linux-6.18.8/kernel/get_addr.c`

3. Программа для проверки системного вызова находится в `root/get_physical_addr.c` и собрана статически: `gcc get_physical_addr.c -static -o get_addr_program`