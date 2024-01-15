import time
import argparse
parser = argparse.ArgumentParser()
parser.add_argument("topic", help="The topic to filter for")
args = parser.parse_args()

topic = args.topic

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
    if len(ret) == 0: print("WARNING: nothing passed the filter")
    return ret 

def count_irqs(table): 
    count = 0;
    for row in table:
        for i in range(1, len(row) - 3):
            count += int(row[i])
    return count

def count_irqs_per_core(table):
    count = [0 for i in range(1, len(table[0]) - 2)]
    for row in table:
        for i in range(1, len(row) - 3):
            count[i] += int(row[i])
    return count

def diff(a, b):
    ret = []
    for i in range(0, len(a)):
        ret += [a[i] - b[i]]
    return ret

def irq_rate():
    table = tabularize("/proc/interrupts")
    table = filter_topic(table, topic)

    old_count = count_irqs(table)
    old_counts = count_irqs_per_core(table)
    while(True):
        table = tabularize("/proc/interrupts")
        table = filter_topic(table, topic)

        new_count = count_irqs(table)
        count = new_count - old_count
        old_count = new_count

        new_counts = count_irqs_per_core(table)
        counts = diff(new_counts, old_counts)
        old_counts = new_counts
        print(f"irq/s: {count}\t max/core: {max(counts)}", flush=True)
        time.sleep(1)

        
irq_rate()

