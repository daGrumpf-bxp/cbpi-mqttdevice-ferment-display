# Host-side test of cbpi_proto.cpp against real CBPi4 payloads.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -DARDUINOJSON_DECODE_UNICODE=0
ARDUINOJSON_URL = https://github.com/bblanchon/ArduinoJson/releases/download/v7.2.0/ArduinoJson-v7.2.0.h

SRC = ../src/state.cpp ../src/cbpi_proto.cpp Arduino.cpp test_main.cpp

.PHONY: test clean

test: test_proto
	./test_proto

test_proto: $(SRC) ArduinoJson.h Arduino.h fake_secrets/secrets.h
	$(CXX) $(CXXFLAGS) -I. -I../src -I../include -Ifake_secrets \
	    $(SRC) -o $@

ArduinoJson.h:
	curl -sL $(ARDUINOJSON_URL) -o $@

fake_secrets/secrets.h:
	@mkdir -p fake_secrets
	@echo '#pragma once' > $@
	@echo '#define FERMENTER_ID "HEYPZ4kMQDuQcWxLU26vFp"' >> $@

clean:
	rm -f test_proto ArduinoJson.h
	rm -rf fake_secrets
