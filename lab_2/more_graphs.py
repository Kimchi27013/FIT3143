import matplotlib.pyplot as plt

# X-axis: Number of Cores/Threads
cores = [1, 2, 4, 6, 8]

# Actual Data extracted for n = 100,000,000
posix_100m =  [1, 1.949884, 3.052587, 4.173318, 4.223778]
openmp_100m = [1, 1.564000, 2.833000, 4.903000, 6.151000]
mpi_100m =    [1, 1.614836, 2.881473, 3.666213, 4.555933]
hybrid_100m = [1, 1.603280, 2.288842, 3.370384, 4.265868]

# Theoretical Data extracted from Amdahl's Law table for n = 100,000,000
amdahl_task1 = [1, 1.771107, 2.882445, 3.644791, 4.200227]
amdahl_task2 = [1, 1.976855, 3.864271, 5.668184, 7.394016]

# Styling configuration
styles = {
    'POSIX':        {'label': 'POSIX Threads (Week 4)', 'color': '#d62728', 'marker': 'v', 'linestyle': '-'},
    'OpenMP':       {'label': 'OpenMP (Week 4)',        'color': '#2ca02c', 'marker': 's', 'linestyle': '-'},
    'MPI':          {'label': 'Open MPI (Task 1)',      'color': '#1f77b4', 'marker': 'o', 'linestyle': '-'},
    'Hybrid':       {'label': 'Hybrid (Task 2)',        'color': '#ff7f0e', 'marker': 'D', 'linestyle': '-'},
    'Amdahl_Task1': {'label': "Amdahl's Law (Task 1)",  'color': '#1f77b4', 'marker': '^', 'linestyle': '--', 'alpha': 0.7},
    'Amdahl_Task2': {'label': "Amdahl's Law (Task 2)",  'color': '#ff7f0e', 'marker': '^', 'linestyle': '--', 'alpha': 0.7}
}

# Create a 1x3 grid of subplots for the three required graphs
fig, axes = plt.subplots(1, 3, figsize=(18, 6))
fig.suptitle("Scaling Performance Analysis (n = 100,000,000)", fontsize=16, fontweight='bold')

# Helper function to format each subplot (Removed the Ideal Linear line)
def format_axis(ax, title):
    ax.set_title(title, fontsize=13, fontweight='bold')
    ax.set_xlabel("Number of Cores / Threads", fontsize=11)
    ax.set_ylabel("Speedup", fontsize=11)
    ax.set_xticks(cores)
    ax.grid(True, linestyle='--', alpha=0.6)

# ---------------------------------------------------------
# Graph 3: Open MPI vs Week 4 (POSIX & OpenMP)
# ---------------------------------------------------------
format_axis(axes[0], "Graph 3: Open MPI vs. Week 4")
axes[0].plot(cores, amdahl_task1, **styles['Amdahl_Task1'])
axes[0].plot(cores, mpi_100m, **styles['MPI'])
axes[0].plot(cores, posix_100m, **styles['POSIX'])
axes[0].plot(cores, openmp_100m, **styles['OpenMP'])
axes[0].legend(loc='upper left', fontsize=10)

# ---------------------------------------------------------
# Graph 4: Hybrid vs Open MPI
# ---------------------------------------------------------
format_axis(axes[1], "Graph 4: Hybrid vs. Open MPI")
axes[1].plot(cores, amdahl_task1, **styles['Amdahl_Task1'])
axes[1].plot(cores, amdahl_task2, **styles['Amdahl_Task2'])
axes[1].plot(cores, hybrid_100m, **styles['Hybrid'])
axes[1].plot(cores, mpi_100m, **styles['MPI'])
axes[1].legend(loc='upper left', fontsize=10)

# ---------------------------------------------------------
# Graph 5: Hybrid vs Week 4 (POSIX & OpenMP)
# ---------------------------------------------------------
format_axis(axes[2], "Graph 5: Hybrid vs. Week 4")
axes[2].plot(cores, amdahl_task2, **styles['Amdahl_Task2'])
axes[2].plot(cores, hybrid_100m, **styles['Hybrid'])
axes[2].plot(cores, posix_100m, **styles['POSIX'])
axes[2].plot(cores, openmp_100m, **styles['OpenMP'])
axes[2].legend(loc='upper left', fontsize=10)

# Adjust layout and save the image
plt.tight_layout()
plt.subplots_adjust(top=0.88) # Make room for the main title
plt.savefig("graphs_3_4_5_scaling_amdahl.png", dpi=300)
plt.show()

# X-axis: Problem Size (n)
n_values = ['10,000,000', '20,000,000', '50,000,000', '100,000,000']

# CORRECTED: Speedup data extracted from your tables specifically for 8 Cores/Threads
posix_speedup = [5.277, 5.024, 5.029, 4.224] 
openmp_speedup = [5.771, 6.163, 6.034, 6.151]
mpi_speedup = [4.653, 4.236, 4.613, 4.556]

# Setup the plot
plt.figure(figsize=(10, 6))

# Plot the lines with unique markers and colors
plt.plot(n_values, posix_speedup, label='POSIX Threads (Week 4)', color='#d62728', marker='v', linestyle='-', linewidth=2, markersize=8)
plt.plot(n_values, openmp_speedup, label='OpenMP (Week 4)', color='#2ca02c', marker='s', linestyle='-', linewidth=2, markersize=8)
plt.plot(n_values, mpi_speedup, label='Open MPI (Task 1)', color='#1f77b4', marker='o', linestyle='-', linewidth=2, markersize=8)

# Formatting the graph
plt.title('Graph 2: Empirical Speed-up vs. Problem Size (n) at 8 Cores', fontsize=14, fontweight='bold')
plt.xlabel('Problem Size (n)', fontsize=12)
plt.ylabel('Speedup Factor', fontsize=12)
plt.grid(True, linestyle='--', alpha=0.6)
plt.legend(fontsize=11, loc='lower right')
plt.ylim(3.5, 7.0) # Adjusted the Y-axis range to perfectly fit the new data

# Save and display
plt.tight_layout()
plt.savefig('graph_2_speedup_vs_n.png', dpi=300)
plt.show()