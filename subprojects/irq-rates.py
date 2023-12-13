import time

topic = "ice"

def deduplicate_whitespace(s):
    return " ".join(s.split())


def tabularize(file):
    table = []
    with open(file, "r") as file:
        file.readline()
        for line in file.readlines():
            cols = []
            cols = deduplicate_whitespace(line).split()
            table += [cols]
    return table


def filter_topic(table, topic):
    ret = []
    for row in table:
        if len(row) <= 3:
            continue
        if topic in row[-1] or topic in row[-2] or topic in row[-3]:
            ret += [row]
    return ret 

def count_irqs(table): 
    count = 0;
    for row in table:
        for i in range(1, len(row) - 3):
            count += int(row[i])
    return count

def irq_count():
    table = tabularize("/proc/interrupts")
    table = filter_topic(table, topic)
    return count_irqs(table)

def irq_rate():
    old_count = irq_count()
    while(True):
        new_count = irq_count()
        count = new_count - old_count
        old_count = new_count
        print(count, flush=True)
        time.sleep(1)

        
irq_rate()

