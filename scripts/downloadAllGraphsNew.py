import boto3
from botocore import UNSIGNED
from botocore.config import Config
import os
import sys

if len(sys.argv) != 2:
    print("Usage: python downloadAllGraphs.py outputFolder")
    exit(1)

output_folder = sys.argv[1]

# In Boto3, anonymous access requires a specific Config object
s3 = boto3.resource("s3", config=Config(signature_version=UNSIGNED))
bucket = s3.Bucket("mqlibinstances")

# Ensure output directory exists
if not os.path.exists(output_folder):
    os.makedirs(output_folder)

# bucket.objects.all() replaces bucket.list()
for obj in bucket.objects.all():
    name = obj.key
    fname = os.path.join(output_folder, name)

    if os.path.isfile(fname):
        print(f"[ Skipping {name} ]")
        continue

    print(f"{name} ...")
    success = False

    # Retry loop
    for count in range(10):
        try:
            # download_file replaces get_contents_to_filename
            bucket.download_file(name, fname)
            success = True
            break
        except Exception as e:
            print(f"  download failed: {e}")

    if not success:
        print(f"Failed to download {name}")
        sys.exit(1)
