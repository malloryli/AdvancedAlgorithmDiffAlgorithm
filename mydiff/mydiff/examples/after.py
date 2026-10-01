import sys

def load(path):
    with open(path, encoding="utf-8") as f:
        return [line.rstrip("\n") for line in f]

def count_words(lines):
    return sum(len(line.split()) for line in lines)

def count_chars(lines):
    return sum(len(line) for line in lines)

def main():
    if len(sys.argv) != 2:
        print("usage: wc.py FILE", file=sys.stderr)
        sys.exit(2)
    lines = load(sys.argv[1])
    print(len(lines), count_words(lines), count_chars(lines))

if __name__ == "__main__":
    main()
