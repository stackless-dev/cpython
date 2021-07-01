import stackless


class MyChannel:

    def __init__(self):
        self.queue = []
        self.balance = 0
        self.temp = None

    def send(self, data):
        if self.balance < 0:
            receiver = self.queue.pop(0)
            self.temp = data
            receiver.insert()
            self.balance += 1
            receiver.run()
        else:
            sender = stackless.current
            self.queue.append((sender, data))
            self.balance += 1
            stackless.schedule_remove()

    def receive(self):
        if self.balance > 0:
            sender, retval = self.queue.pop(0)
            sender.insert()
            self.balance -= 1
            return retval
        else:
            receiver = stackless.current
            self.queue.append(receiver)
            self.balance -= 1
            stackless.schedule_remove()
            return self.temp


def f1(ch):
    for i in range(5):
        ch.send(i)
    print("done sending")


def f2(ch):
    while 1:
        data = ch.receive()
        if data is None:
            print("done receiving")
            return
        print("received", data)


def test():
    ch = MyChannel()
    t2 = stackless.tasklet(f2)(ch)
    t1 = stackless.tasklet(f1)(ch)
    stackless.run()
    return ch

if __name__ == "__main__":
    test()
