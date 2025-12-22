import json
import random

# Tag list based on your mapping
tags = [
    "E2801170000002076A50957C",
    "E2801170000002076A508C7E",
    "E2801170000002076A509570",
    "E2801170000002076A50957A",
    "E2801170000002076A508C7D",
    "E2801170000002076A50957B",
    "E2801170000002076A50957D",
    "E2801170000002076A508D78",
    "E2801170000002076A509C7C",
    "E2801170000002076A509478",
    "E2801170000002076A509477",
    "E2801170000002076A509476"
]

# Function to generate a single burst of 6 entries (NO RSSI, NO ANTENNA)
def generate_burst(start_time):
    burst = []
    for i in range(6):
        tag = random.choice(tags)
        time = start_time + random.randint(0, 1000)
        burst.append({
            "tag": tag,
            "time": time
        })
    return burst

# Prompt user for number of bursts
while True:
    try:
        num_bursts = int(input("Enter the number of bursts to generate: "))
        if num_bursts <= 0:
            raise ValueError
        break
    except ValueError:
        print("Please enter a valid positive integer.")

# Generate bursts
all_bursts = []
start_time = 1708456123456

for b in range(num_bursts):
    burst = generate_burst(start_time)
    all_bursts.append({"burst": burst})
    start_time += 1000  # increment base time for next burst

# Write to a .txt file in the requested format
with open('example_rfid_bursts.txt', 'w') as f:
    for burst_block in all_bursts:
        f.write('{ "burst": [\n')
        for entry in burst_block["burst"]:
            line = f'{{"tag":"{entry["tag"]}", "time":{entry["time"]}}},\n'
            f.write(line)
        f.write(']}\n\n')

print(f"File 'example_rfid_bursts.txt' generated successfully with {num_bursts} bursts.")
