TARGET = goldstard
OBJ = main.o \
  AudioWidget.o Hardware.o Player.o \
  SlaveProcess.o RemoteControl.o Broadcaster.o \
  PipedResource.o ControlResource.o
LIBS = -lwt -lwthttp -lpthread
CC = g++
CXXFLAGS = -std=c++14 -O3 -include wt.hpp -DAPPNAME=\"goldstard\"
LDFLAGS =

all: $(TARGET)

wt.hpp.gch:
	$(CC) $(CXXFLAGS) wt.hpp

%.o: %.cpp wt.hpp.gch
	$(CC) $(CXXFLAGS) -c -o $@ $<

$(TARGET): wt.hpp.gch $(OBJ)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJ) $(LIBS)

install: all
	cp $(TARGET) /usr/local/bin

clean:
	$(RM) $(TARGET) *.o *.gch
