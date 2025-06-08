import sys
import subprocess
import requests
# update Device Id, cert files inside ./data directory

API_URL = "https://b1rklukfdi.execute-api.eu-central-1.amazonaws.com/setup/provision/create-device"

# read api key fromm /scriptts/secrets.txt
try:
    with open("scripts/secrets.txt", "r") as f:
        api_key = f.read().strip()
except Exception as e:
    print("Error reading api key, check documentation for how to add api key")
    print(e)
    exit(1)
    
# Make API call to get device id and cert
try:
    response = requests.post(API_URL, headers={"x-api-key": api_key})
    response = response.json()
    device_id = response["device_id"]
    cert = response["X-Certificate-Pem"]
    print("Got device id and cert")
except Exception as e:
    print("Error getting device id and cert")
    print(e)
    exit(1)
    
print("Got Device id: ", device_id)

# certs needs to be stripped of new line characters, as well as -----BEGIN CERTIFICATE----- and -----END CERTIFICATE-----
cert = cert.replace("\n", "")
cert = cert.replace("-----BEGIN CERTIFICATE-----", "")
cert = cert.replace("-----END CERTIFICATE-----", "")


# update device id, replace with the device id of the new device
try:
    with open("data/device_id.txt", "w") as f:
        f.write(device_id)
    print("Updated device id")
except Exception as e:
    print("Error updating device id")
    print(e)
    exit(1)
    
# update cert file, replace with the cert file of the new device
try:
    with open("data/cert.txt", "w") as f:
        f.write(cert)
    print("Updated cert file")
except Exception as e:
    print("Error updating cert file")
    print(e)
    exit(1)
    
# flash the new device, run idf.py build flash -p PORT, expect port to be provided as an argument
try:
    PORT = sys.argv[1]
    if sys.platform == "win32":
        subprocess.run(["idf.py", "build", "flash", "-p", PORT], shell=True, check=True)
    elif sys.platform == "linux":
        subprocess.run(["idf.py", "build", "flash", "-p", PORT], check=True)
    print("Flashed new device")
except subprocess.CalledProcessError as e:
    print("Error flashing new device")
    print(e)
    exit(1)
    


