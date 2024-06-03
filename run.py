import os

# datasets and columns to be compressed
datasets = {
    "zup_export_2023-06-16_170948.csv": "3",
    "vykony_export_2023-06-16_170946.csv": "3,5",
    "pripady_export_2023-06-16_170933.csv": "10",
    "nesreca_export_2023-06-16_171742.csv": "18,19",
    "EXPENDITURES_export_2023-06-16_172225.csv": "0",
    "HOUSEHOLD_MEMBERS_export_2023-06-16_172239.csv": "0",
    "account_export_2023-06-16_172336.csv": "0",
    "client_export_2023-06-16_172349.csv": "0",
    "disp_export_2023-06-16_172351.csv": "0",
    "postLinks_export_2023-06-16_171539.csv": "0,2,3",
    "votes_export_2023-06-16_171548.csv": "0,1"
}

# set batch size
batch_size = 50
base_dir = "./datasets/"

# run
for dataset, columns in datasets.items():
    input_file = os.path.join(base_dir, dataset)
    command = f"./build/IntegerConvertor --InputFile={input_file} --BatchSize={batch_size} --Columns={columns}"
    print(f"Execute: {command}")
    os.system(command)