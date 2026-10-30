.PHONY: test flash
test:
	c++ -std=c++17 -O2 -Wall -Wextra -Ifirmware/lib/seismic firmware/test/test_seismic.cpp -o /tmp/test_seismic && /tmp/test_seismic
	python3 -m unittest -v
flash:
	cd firmware && pio run -e heltec_v3 -t upload
