import re
import numpy as np
import os

def analyze_log(file_name):
    """
    Reads a log file with the new format, extracts timestamps for image acquisition,
    conversion, and writing, then calculates individual and combined statistics,
    and finally estimates Frames Per Second (FPS).

    Args:
        file_name (str): The name of the log file to analyze.

    Returns:
        None: Prints the analysis results or an error message.
    """
    acquired_times = []
    converted_times = []
    wrote_times = []

    try:
        with open(file_name, 'r') as f:
            for line in f:
                if "acquired image in" in line:
                    match = re.search(r'acquired image in (\d+\.\d+) seconds', line)
                    if match:
                        acquired_times.append(float(match.group(1)))
                elif "converted to sharpened image in" in line:
                    match = re.search(r'converted to sharpened image in (\d+\.\d+) seconds', line)
                    if match:
                        converted_times.append(float(match.group(1)))
                elif "wrote image(s) in" in line:
                    match = re.search(r'wrote image\(s\) in (\d+\.\d+) seconds', line)
                    if match:
                        wrote_times.append(float(match.group(1)))

    except FileNotFoundError:
        print(f"Error: File '{file_name}' not found. Please ensure the file is in the correct directory.")
        return
    except Exception as e:
        print(f"An error occurred while reading the file '{file_name}': {e}")
        return

    # Function to print statistics for a given list of times
    def print_stats(name, times_list):
        if not times_list:
            print(f"No '{name}' entries found.")
            return

        mean_val = np.mean(times_list)
        min_val = np.min(times_list)
        median_val = np.median(times_list)
        max_val = np.max(times_list)

        print(f"\n--- Statistics for {name} ---")
        print(f"Mean: {mean_val:.6f} seconds")
        print(f"Min: {min_val:.6f} seconds")
        print(f"Median: {median_val:.6f} seconds")
        print(f"Max: {max_val:.6f} seconds")
        print("-----------------------------------")

    # Print individual statistics
    print_stats("Image Acquisition Time", acquired_times)
    print_stats("Image Conversion Time", converted_times)
    print_stats("Image Write Time", wrote_times)

    # Calculate combined times and statistics
    total_frame_times = []
    num_complete_frames = min(len(acquired_times), len(converted_times), len(wrote_times))

    if num_complete_frames > 0:
        for i in range(num_complete_frames):
            total_frame_times.append(
                acquired_times[i] + converted_times[i] + wrote_times[i]
            )
        print_stats("Total Time Per Frame (Combined)", total_frame_times)

        # Calculate Frames Per Second (FPS)
        mean_total_time = np.mean(total_frame_times)
        if mean_total_time > 0:
            fps = 1 / mean_total_time
            print(f"\nEstimated Frames Per Second (FPS): {fps:.2f}")
        else:
            print("\nCannot calculate FPS: Mean total time per frame is zero.")
    else:
        print("\nNot enough complete frame data to calculate combined statistics or FPS.")

if __name__ == "__main__":
    # Prompt the user for the log file name
    log_file_to_analyze = input("Please enter the name of the log file to analyze: ")
    analyze_log(log_file_to_analyze)