import os
import sys
import time


# протокол взаимодействия
# kernel space -> user space: cmd <id> (<msg>)
# user space -> kernel space: - ERROR: txt при ошибке
#                             - txt  иначе
read_fifo_path = "/tmp/module_reqs"
write_fifo_path = "/tmp/messenger_resps"

msg_len_limit = 4096
msg_history_limit = 10

chat_1 = [
    "[Aleksey] Hello everybody!",
    "[Artem] yoo!",
    "[Ivan] When do we have ml deadline?",
    "[Grigoriy] idk, but we should probably start doing it"
]

chat_2 = [
    "[a] aaaaaaaaaaa.",
    "[b] bbbbbbbbbbbbbbbbbbbbbbb?",
    "[c] ccccccccccc ccccccccccc - ccc c c c c c cc c c c c c c c",
    "[d] dddddddddddddddddddd: ddddddddddddddddddd",
    "[c] cccc ccccccccccccc c c ccccccccccccccccccccccccc!!!!!!!!!!"
]

chat_3 = [
    "[kiborg420] go play dota",
    "[ubivator3000] go"
]

chat_db = {1: chat_1, 2: chat_2, 3: chat_3}


def handle_read(id):
    if id > 3 or id < 1:
        return "ERROR: chat id must be 1, 2 or 3"
    
    msgs = '\n'.join(chat_db[id][-msg_history_limit:])
    msgs += '\n'
    return msgs
    
def handle_write(id, msg):
    if id > 3 or id < 1:
        return "ERROR: chat id must be 1, 2 or 3"
    
    if len(msg) > msg_len_limit:
        return "ERROR: message is too long"
    
    chat_db[id].append("[Я] " + msg)
    return ""

    
def handle_line(line):
    line = line.strip()

    if not line:
        return "ERROR: no command to process"

    line = line.split(' ', 2)

    if line[0] == 'read':
        if len(line) < 2:
            return "ERROR: read operation failed: <chat id> is not specified"
        
        try:
            chat_id = int(line[1])
        except:
            return "ERROR: <chat id> must be a number"
        
        return handle_read(chat_id)
    
    elif line[0] =='write':
        if len(line) < 3:
            return "ERROR: write operation failed: 2 arguments required: <chat id> <message>"

        try:
            chat_id = int(line[1])
        except:
            return "ERROR: <chat id> must be a number"
        
        msg = line[2]
        return handle_write(chat_id, msg)
    
    else:
        return "ERROR: unknown command. there are only 2 commands: write and read"

def create_fifo(path):
    if os.path.exists(path):
        os.remove(path)
    os.mkfifo(path)


def main():
    create_fifo(read_fifo_path)
    create_fifo(write_fifo_path)

    read_fifo = open(read_fifo_path, 'r')
    while True:

        for line in read_fifo:
            resp = handle_line(line)
            write_fifo = open(write_fifo_path, 'w')

            write_fifo.write(resp + '\n')
            write_fifo.flush()

            write_fifo.close()


if __name__ == "__main__":
    main()