import sys

def load(path):
    with open(path) as f:
        return [line.rstrip("\n") for line in f]

def count_words(lines):
    total = 0
    for line in lines:
        total += len(line.split())
    return total

def main():
    if len(sys.argv) != 2:
        print("usage: wc.py FILE")
        sys.exit(1)
    lines = load(sys.argv[1])
    print(len(lines), count_words(lines))

if __name__ == "__main__":
    main()
