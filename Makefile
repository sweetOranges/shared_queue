consumer: shared_queue.h consumer.cpp
	g++ -std=c++11 ./consumer.cpp -lpthread -o ./build/consumer
producer: shared_queue.h producer.cpp
	g++ -std=c++11 ./producer.cpp -lpthread -o ./build/producer

all: consumer producer

clean:
	-rm -rf ./build/consumer
	-rm -rf ./build/producer

.PHONY : clean all