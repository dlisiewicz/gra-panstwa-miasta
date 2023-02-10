import tkinter as tk
from tkinter import *
import socket
import time
import threading
import sys
import argparse


class Client:
    def __init__(self, port=1234, ip=socket.gethostbyname("127.0.0.1")):
        self.parser = argparse.ArgumentParser()
        self.parser.add_argument("ip", help="ip")
        self.parser.add_argument("port", help="port")
        self.args = self.parser.parse_args()

        self.fullbuf = ""
        self.server_ip = socket.gethostbyname(self.args.ip)
        self.server_port = int(self.args.port)
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.connect((self.server_ip, self.server_port))
        self.read_thread = threading.Thread(target=self.receive_msg)
        self.read_thread.start()
        self.handle_msg_thread = threading.Thread(target=self.fBuff)
        self.handle_msg_thread.start()

    def send_msg(self, message):
        self.server.send(bytes(message, "utf-8"))

    def receive_msg(self):

        while True:
            try:
                buffer = self.server.recv(1024).decode("utf-8")
                if len(buffer) > 0:
                    self.fullbuf += buffer

            except:
                self.server.close()
                sys.exit()

    def fBuff(self):
        while True:
            for i in range(len(self.fullbuf)):
                if self.fullbuf[i] == '\n':
                    handleMsg(self.fullbuf[:i])
                    self.fullbuf = self.fullbuf[i + 1:]
                    break


client = Client()


class App(tk.Tk):
    def __init__(self, *args, **kwargs):
        self.counterSeconds = 0
        self.counter_thread = threading.Thread(target=self.counter)
        self.counter_thread.start()

        tk.Tk.__init__(self, *args, **kwargs)
        self.title("Państwa-miasta")
        self.geometry("700x400")
        self.option_add("*Label.Font", "Helvetica 12")
        self.option_add("*Listbox.Font", "Helvetica 12")

        container = tk.Frame(self)
        container.pack(side="top", fill="both", expand=True)
        container.grid_rowconfigure(0, weight=1)
        container.grid_columnconfigure(0, weight=1)
        self.frames = {}
        for F in (NicknameFrame, GameFrame):
            page_name = F.__name__
            frame = F(parent=container)
            self.frames[page_name] = frame
            frame.grid(row=0, column=0, sticky="nsew")

        self.show_frame('NicknameFrame')

    def show_frame(self, page_name):
        frame = self.frames[page_name]
        frame.tkraise()

    def counter(self):
        while True:
            if (self.counterSeconds != 0):
                self.frames['GameFrame'].time_label.config(text="Czas:" + str(self.counterSeconds) + "s")
                time.sleep(1)
                self.counterSeconds -= 1

    def sendNickname(self, entry):
        nickname = entry.get()
        client.send_msg('N' + nickname)

    def changeNicknameLabel(self):
        self.frames['NicknameFrame'].error_label.config(text='Podany nick już istnieje')

    def updateCheckBox(self, list):
        list = list[:len(list) - 1].split(";")
        self.frames['GameFrame'].listbox.delete(0, END)
        for i in list:
            self.frames['GameFrame'].listbox.insert(self.frames['GameFrame'].listbox.size() + 1, i)

    def gameStarted(self, letter):
        self.frames['GameFrame'].button["state"] = "normal"
        self.counterSeconds = 45
        self.frames['GameFrame'].letter_label.config(text='Litera to: ' + str(letter))
        self.frames['GameFrame'].country_entry.delete(0, END)
        self.frames['GameFrame'].city_entry.delete(0, END)
        self.frames['GameFrame'].name_entry.delete(0, END)

    def sendAnswers(self, country_entry, city_entry, name_entry):
        country = country_entry.get()
        city = city_entry.get()
        name = name_entry.get()
        self.frames['GameFrame'].button["state"] = "disabled"
        client.send_msg('A' + country + ";" + city + ";" + name)


class NicknameFrame(tk.Frame):
    def __init__(self, parent):
        tk.Frame.__init__(self, parent)
        self.nickname_label = tk.Label(self, text="Podaj nick")
        self.nickname_label.pack(side="top", fill="x", pady=10)
        self.nickname_entry = tk.Entry(self)
        self.nickname_entry.pack(padx=5, pady=(5, 15))
        self.nickname_button = tk.Button(self, text="Potwierdz", command=lambda: app.sendNickname(self.nickname_entry))
        self.nickname_button.pack()
        self.error_label = tk.Label(self, text="")
        self.error_label.pack(side="top", fill="x", pady=10)


class GameFrame(tk.Frame):
    def __init__(self, parent):
        tk.Frame.__init__(self, parent)

        self.listbox = tk.Listbox(self, width=1)
        self.listbox.config(width=20)
        self.listbox.pack(ipadx=20, ipady=20, fill=tk.BOTH, expand=False, side=tk.LEFT)

        self.letter_label = tk.Label(self, text="Litera to:")
        self.letter_label.pack()
        self.time_label = tk.Label(self, text="Czas: 0s")
        self.time_label.pack()
        self.country_label = tk.Label(self, text="Państwo")
        self.city_label = tk.Label(self, text="Miasto")
        self.name_label = tk.Label(self, text="Imie")

        self.country_entry = tk.Entry(self)
        self.city_entry = tk.Entry(self)
        self.name_entry = tk.Entry(self)
        self.country_label.pack()
        self.country_entry.pack()
        self.city_label.pack()
        self.city_entry.pack()
        self.name_label.pack()
        self.name_entry.pack()

        self.button = tk.Button(self, text="Zatwierdź", command=lambda: app.sendAnswers(self.country_entry,
                                                                                        self.city_entry,
                                                                                        self.name_entry))
        self.button.pack()
        self.button["state"] = "disabled"


def handleMsg(msg):
    if msg == 'valid':
        app.show_frame('GameFrame')
    elif msg == 'invalid':
        app.changeNicknameLabel()
    elif msg[:5] == 'start':
        app.gameStarted(msg[5])
    else:
        app.updateCheckBox(msg)


if __name__ == "__main__":
    app = App()
    app.mainloop()
