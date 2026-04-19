CC = g++
CFLAGS = -Wall -std=c++11 -O3
LDFLAGS = -lncurses -pthread

TARGET = wb
SRC = wb.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install:
	cp ${TARGET} /usr/local/bin/
