import re
import numpy as np
import os

def analyze_log_file(file_name):
    """
    Reads a log file, extracts 'read_frame took <time> seconds' timestamps,
    and calculates mean, min, median, and max of these times.

    Args:
        file_name (str): The name of the log file to analyze.

    Returns:
        None: Prints the analysis results or an error message.
    """
    timestamps = []
    try:
        with open(file_name, 'r') as f:
            for line in f:
                match = re.search(r'Processing time: (\d+\.\d+) seconds', line)
                if match:
                    timestamps.append(float(match.group(1)))
    except FileNotFoundError:
        print(f"Error: File '{file_name}' not found. Please ensure the file is in the correct directory.")
        return
    except Exception as e:
        print(f"An error occurred while reading the file '{file_name}': {e}")
        return

    if not timestamps:
        print(f"No 'read_frame took <time> seconds' entries found in '{file_name}'.")
        return
    else:
        mean_time = np.mean(timestamps)
        min_time = np.min(timestamps)
        median_time = np.median(timestamps)
        max_time = np.max(timestamps)

        print(f"\n--- Analysis Results for: {file_name} ---")
        print(f"Mean read_frame time: {mean_time:.6f} seconds")
        print(f"Min read_frame time: {min_time:.6f} seconds")
        print(f"Median read_frame time: {median_time:.6f} seconds")
        print(f"Max read_frame time: {max_time:.6f} seconds")
        print("------------------------------------------")
        print(f"Mean read_frame FPS: {1.0/mean_time:.6f} fps")
        print(f"Min read_frame FPS: {1.0/min_time:.6f} fps")
        print(f"Median read_frame fps: {1.0/median_time:.6f} fps")
        print(f"Max read_frame fps: {1.0/max_time:.6f} fps")
        print("------------------------------------------")

if __name__ == "__main__":
    # You can change 'your_log_file.log' to the name of your log file.
    # For example: analyze_log_file('pgm-output.log')
    # If your log file is in a different directory, provide the full path.
    log_file_to_analyze = input("Please enter the name of the log file to analyze (e.g., pgm-output.log): ")
    analyze_log_file(log_file_to_analyze)