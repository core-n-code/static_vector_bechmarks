import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

def main():
    # Load data
    df = pd.read_csv("results.txt")

    # Available containers in data
    containers = df['Container'].unique()
    sizes = df['Size'].unique()

    # Metrics to plot
    metrics = [
        ('CreateDestroyMean', 'Create+Destroy'),
        ('IterateMean', 'Iterate'),
        ('AccessMean', 'Access')
    ]
    ci_cols = [
        'CreateDestroyCI95',
        'IterateCI95',
        'AccessCI95'
    ]

    fig, axes = plt.subplots(1, 3, figsize=(18, 5), sharex=True)

    colors = ['tab:blue', 'tab:orange']

    for ax, (metric, title), ci_col in zip(axes, metrics, ci_cols):
        for i, container in enumerate(containers):
            subset = df[df['Container'] == container]
            sizes = subset['Size']
            mean = subset[metric]
            ci = subset[ci_col]

            ax.plot(sizes, mean, marker='o', label=container, color=colors[i])
            ax.fill_between(sizes,
                            mean - ci,
                            mean + ci,
                            color=colors[i],
                            alpha=0.18)

        ax.set_xscale('log')
        ax.set_yscale('log')
        ax.set_title(title)
        ax.set_xlabel('Container Size')
        ax.set_ylabel('Time (ms)')
        ax.legend()
        ax.grid(True, which="both", ls='--', alpha=0.5)

    plt.tight_layout()
    plt.suptitle('static_vector vs vector<unique_ptr>', fontsize=16, y=1.07)
    plt.savefig('benchmark_blog_plot.png', dpi=200, bbox_inches='tight')
    plt.show()

if __name__ == "__main__":
    main()
