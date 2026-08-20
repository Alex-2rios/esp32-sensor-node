.DEFAULT_GOAL := help
.PHONY: help build test check upload monitor clean

help:
	@echo "build    compile the firmware for the esp32"
	@echo "test     run the unit tests on this machine, no board needed"
	@echo "check    run static analysis"
	@echo "upload   flash the board and the filesystem image"
	@echo "monitor  open the serial monitor"
	@echo "clean    remove the build directory"

build:
	pio run -e esp32dev

test:
	pio test -e native

check:
	pio check -e esp32dev --skip-packages --fail-on-defect high

upload:
	pio run -e esp32dev -t upload
	pio run -e esp32dev -t uploadfs

monitor:
	pio device monitor

clean:
	pio run -t clean
	rm -rf .pio
