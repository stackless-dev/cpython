import stackless


class MyChannel:

    def __init__(self):
        self.queue = []
        self.balance = 0

    def send(self, data):
        if self.balance < 0:
            receiver = self.queue.pop(0)
            receiver.tempval = data
            receiver.insert()
            self.balance += 1
            receiver.run()
        else:
            sender = stackless.current
            self.queue.append(sender)
            self.balance += 1
            stackless.schedule_remove(data)

    def receive(self):
        if self.balance > 0:
            sender = self.queue.pop(0)
            retval = sender.tempval
            sender.tempval = None
            sender.insert()
            self.balance -= 1
            return retval
        else:
            receiver = stackless.current
            self.queue.append(receiver)
            self.balance -= 1
            retval = stackless.schedule_remove()
            return retval


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
