# Supermoan

A Linux program that plays sound based on mouse movement intensity. The program monitors mouse movement events and plays corresponding sound files based on the movement's intensity level.

Original program called nubmoan from [wttdotm](https://github.com/wttdotm/nubmoan)\
Linux variant from [shasherazi](https://github.com/shasherazi/nubmoan)

All credit goes to these developers, I just made it SuperMoan🤠
![April Kepner](moan.gif)

## Features

- Real-time mouse movement tracking
- Logarithmic scaling of movement intensity
- Configurable movement thresholds and scaling parameters
- Sound playback based on movement intensity (10 different levels)
- Debug mode with detailed statistics
- Device listing functionality
- Test mode without sound playback

## Prerequisites

The program requires:
- GCC compiler
- ALSA sound system (for sound playback)
- pthread library
- math library

## Installation

1. Clone or download the source code
2. Compile the program using:
```bash
gcc -Wall -g -lm -pthread -o supermoan supermoan.c
```

## Usage

Basic usage:
```bash
./supermoan -i <device_path>
```

### Command Line Options

| Option | Long Option | Description |
|--------|-------------|-------------|
| -l | --list-devices | List all available input devices |
| -i | --input <device> | Specify input device path (required) |
| -d | --debug | Enable debug output |
| -m | --min-threshold N | Set minimum movement threshold (default: 1.0) |
| -M | --max-threshold N | Set maximum movement threshold (default: 100.0) |
| -b | --log-base N | Set logarithm base for scaling (default: 2.0) |
| -n | --no-sound | Don't play sound files (for testing) |
| -h | --help | Display help message |

### Examples

1. List available input devices:
```bash
./supermoan --list-devices
```

2. Run with a specific input device:
```bash
./supermoan -i /dev/input/event2
```

3. Run with debug output:
```bash
./supermoan -i /dev/input/event2 --debug
```

4. Run with custom thresholds:
```bash
./supermoan -i /dev/input/event2 --min-threshold 2.0 --max-threshold 150.0
```

5. Test mode (no sound):
```bash
./supermoan -i /dev/input/event2 --no-sound
```

6. Test mode with debug (no sound):
```bash
./supermoan -i /dev/input/event2 --no-sound --debug
```

## Sound Files

The program expects sound files to be present in the `moanswav` directory, named from 1.wav to 10.wav. Each file corresponds to a different intensity level:
- 1.wav: lowest intensity
- 10.wav: highest intensity

## Technical Details

### Movement Intensity Calculation

The program calculates movement intensity using the following approach:

1. Calculates Euclidean distance of mouse movement (dx, dy)
2. Applies configurable thresholds:
   - Movements below min_threshold return intensity level 1
   - Movements above max_threshold return intensity level 10
3. Uses logarithmic scaling for values between thresholds
4. Maps the scaled value to intensity levels 1-10

### Debug Statistics

When running in debug mode (-d), the program provides:
- Distribution of intensity levels
- Total movement count
- Visual histogram of intensity distribution
- Real-time movement and scaling values

## Error Handling

The program includes error handling for:
- Invalid device paths
- Device access permissions
- Sound file playback issues
- Invalid command line arguments
- Configuration parameter validation

## Clean Exit

The program handles SIGINT (Ctrl+C) gracefully:
- Stops mouse monitoring
- Terminates sound playback
- Prints debug statistics (if enabled)
- Cleans up resources

## Notes

- Requires appropriate permissions to access input devices (typically root or input group membership)
- Sound files must be compatible with ALSA's aplay command