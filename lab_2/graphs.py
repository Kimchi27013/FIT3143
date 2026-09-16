import io

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


# Define the x-axis (Number of Cores)
cores = [1, 2, 4, 6, 8]
n_values = ["10000000", "20000000", "50000000", "100000000"]

# Task 1 - MPI (Actual Speedup)
task1_actual = {
    "10000000": [1, 1.806185, 2.856226, 3.449414, 4.652987],
    "20000000": [1, 1.702308, 2.623636, 3.785538, 4.236466],
    "50000000": [1, 1.776995, 2.567042, 4.031319, 4.613325],
    "100000000": [1, 1.614836, 2.881473, 3.666213, 4.555933],
}

# Task 2 - OpenMP + MPI (Actual Speedup)
task2_actual = {
    "10000000": [1, 1.661723, 2.517509, 3.527654, 4.589739],
    "20000000": [1, 1.656363, 2.648770, 3.609478, 4.434630],
    "50000000": [1, 1.645792, 2.429844, 3.242786, 4.270606],
    "100000000": [1, 1.603280, 2.288842, 3.370384, 4.265868],
}

# Amdahl's Law Speedup - Task 1
amdahl_task1 = {
    "10000000": [1, 1.602143, 2.292285, 2.676612, 2.921525],
    "20000000": [1, 1.616650, 2.337298, 2.745204, 3.007652],
    "50000000": [1, 1.708479, 2.645685, 3.237713, 3.645604],
    "100000000": [1, 1.771107, 2.882445, 3.644791, 4.200227],
}

# Amdahl's Law Speedup - Task 2
amdahl_task2 = {
    "10000000": [1, 1.932432, 3.620254, 5.107144, 6.426968],
    "20000000": [1, 1.951753, 3.723840, 5.339979, 6.819887],
    "50000000": [1, 1.969512, 3.822484, 5.568962, 7.217871],
    "100000000": [1, 1.976855, 3.864271, 5.668184, 7.394016],
}


def plot_speedup_comparison():
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle("Performance Analysis: Actual vs. Theoretical Speedup", fontsize=16, fontweight="bold")
    axes = axes.flatten()

    styles = {
        "Task 1 - MPI (Actual)": {"color": "#1f77b4", "marker": "o", "linestyle": "-"},
        "Task 2 - OpenMP + MPI (Actual)": {"color": "#ff7f0e", "marker": "s", "linestyle": "-"},
        "Amdahl's Law - Task 1": {"color": "#2ca02c", "marker": "^", "linestyle": "--"},
        "Amdahl's Law - Task 2": {"color": "#d62728", "marker": "D", "linestyle": "--"},
    }

    for i, n in enumerate(n_values):
        ax = axes[i]
        ax.plot(cores, task1_actual[n], label="Task 1 - MPI (Actual)", **styles["Task 1 - MPI (Actual)"])
        ax.plot(cores, task2_actual[n], label="Task 2 - OpenMP + MPI (Actual)", **styles["Task 2 - OpenMP + MPI (Actual)"])
        ax.plot(cores, amdahl_task1[n], label="Amdahl's Law - Task 1", **styles["Amdahl's Law - Task 1"])
        ax.plot(cores, amdahl_task2[n], label="Amdahl's Law - Task 2", **styles["Amdahl's Law - Task 2"])
        ax.plot(cores, cores, label="Ideal Linear Speedup", color="gray", linestyle=":", alpha=0.7)

        ax.set_title(f"Speedup for n = {int(n):,}", fontsize=13)
        ax.set_xlabel("Number of Cores", fontsize=11)
        ax.set_ylabel("Speedup", fontsize=11)
        ax.set_xticks(cores)
        ax.grid(True, linestyle="--", alpha=0.5)

        if i == 0:
            ax.legend(loc="upper left", fontsize=9)

    plt.tight_layout()
    plt.subplots_adjust(top=0.92)
    fig.savefig("speedup_comparison.png", dpi=300)
    plt.close(fig)


def plot_runtime_comparison():
    csv_data = """n,OpenMP,POSIX,OpenMPI,Hybrid
10000000,0.612,3.122439,1.978523,0.55
15000000,1.057,5.507939,3.784644,0.921432
20000000,1.541,8.940446,4.70752,1.276546
25000000,2.098,12.01896,6.417882,1.857376
30000000,2.845,15.34497,8.849056,2.04811
35000000,3.424,18.01059,9.375013,2.490004
40000000,4.321,20.93166,12.35109,3.273438
45000000,5.969,26.67612,17.51142,3.539254
50000000,8.073,29.20158,16.04692,4.03907
55000000,8.648,32.92918,22.04647,5.268575
60000000,9.519,38.56542,20.55656,5.841311
65000000,9.953,50.77778,20.35423,6.978001
70000000,11.127,63.34083,26.5926,7.69253
75000000,12.686,69.10936,25.58662,9.915314
80000000,13.399,75.06888,29.83273,10.05632
85000000,14.74,86.41664,29.58865,9.929227
90000000,15.725,82.33238,32.12139,10.16924
95000000,15.848,98.14127,37.03395,11.03445
100000000,17.048,107.2405,44.51444,11.50506
105000000,19.753,105.3705,40.41814,12.09397
110000000,22.458,126.429,40.87432,13.14562
115000000,24.315,131.2143,49.31888,13.59542
120000000,23.919,136.9946,46.74197,14.81442
125000000,25.842,152.4959,49.89772,15.46285
130000000,28.629,159.6706,55.07229,16.19481
135000000,28.564,166.9634,58.15308,17.1488
140000000,28.489,153.166,52.49884,18.52755
145000000,31.705,202.2426,57.04762,18.91163
150000000,33.576,197.6657,63.5382,20.43017
155000000,36.679,178.1106,68.77669,23.0742
160000000,35.526,171.9727,75.4042,23.93582
165000000,38.374,225.5522,75.31181,25.40284
170000000,43.917,244.5097,76.93842,25.12017
175000000,42.797,251.2293,77.21784,26.24681
180000000,45.875,248.9681,78.14715,26.80381
185000000,49.539,259.2598,79.12617,27.7533
190000000,48.234,262.7427,89.65146,27.64702
195000000,56.165,273.1971,91.21142,28.78022
200000000,62.034,274.2578,93.0496,30.0112"""

    df = pd.read_csv(io.StringIO(csv_data))

    fig, ax = plt.subplots(figsize=(12, 7))
    ax.plot(df["n"], df["POSIX"], label="Week 4: POSIX Threads", color="#d62728", linewidth=2)
    ax.plot(df["n"], df["OpenMPI"], label="Week 8 (Task 1): Open MPI", color="#1f77b4", linewidth=2)
    ax.plot(df["n"], df["OpenMP"], label="Week 4: OpenMP", color="#2ca02c", linewidth=2)
    ax.plot(df["n"], df["Hybrid"], label="Week 8 (Task 2): Hybrid", color="#ff7f0e", linewidth=2, linestyle="--")

    ax.set_title("Run Time vs. Problem Size (n)", fontsize=15, fontweight="bold")
    ax.set_xlabel("Problem Size (n)", fontsize=12)
    ax.set_ylabel("Execution Time (Seconds)", fontsize=12)
    ax.grid(True, linestyle="--", alpha=0.6)
    ax.legend(fontsize=11, loc="upper left")

    ax.set_xticks([25000000, 50000000, 75000000, 100000000, 125000000, 150000000, 175000000, 200000000])
    ax.set_xticklabels(["25M", "50M", "75M", "100M", "125M", "150M", "175M", "200M"])

    plt.tight_layout()
    fig.savefig("runtime_comparison.png", dpi=300)
    plt.close(fig)


if __name__ == "__main__":
    plot_speedup_comparison()
    plot_runtime_comparison()
    print("Created speedup_comparison.png and runtime_comparison.png")