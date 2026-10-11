#!/usr/bin/env bash
# scripts/aws/ec2-probe.sh — launch a Linux box, run the perf-counter probe, fetch
# results, terminate. Usage:  ./ec2-probe.sh <arm64|x86_64> [instance-type]
#   arm64  → c7g.large (Graviton3, closest arm64 to our M4)
#   x86_64 → c7i.large (Intel; standard perf events)
# Requires: aws cli with ec2 perms. Bills the caller's account; terminates on exit.
set -euo pipefail
ARCH="${1:-arm64}"
TYPE="${2:-$([ "$ARCH" = arm64 ] && echo c7g.large || echo c7i.large)}"
REGION="${AWS_REGION:-us-east-1}"
LOB="$(cd "$(dirname "$0")/../.." && pwd)"
NAME="lob-probe-${ARCH}-$$"
KEY="$NAME.pem"
SGNAME="lob-probe-${ARCH}-$$"
OUT="$LOB/scripts/aws/results-${ARCH}-$(date +%Y%m%d-%H%M%S).txt"
mkdir -p "$LOB/scripts/aws"

IID=""; SGID=""
cleanup() {
  echo "-- cleanup --"
  [ -n "$IID" ] && aws ec2 terminate-instances --instance-ids "$IID" --region "$REGION" >/dev/null 2>&1 || true
  [ -n "$SGID" ] && aws ec2 delete-security-group --group-id "$SGID" --region "$REGION" >/dev/null 2>&1 || true
  aws ec2 delete-key-pair --key-name "$KEY" --region "$REGION" >/dev/null 2>&1 || true
  rm -f "/tmp/$KEY" /tmp/lob-src.tgz
}
trap cleanup EXIT

echo "== am i: launch $TYPE ($ARCH) in $REGION =="
AMI_PARAM="/aws/service/ami-amazon-linux-latest/al2023-ami-kernel-default-${ARCH}"
AMI=$(aws ssm get-parameter --name "$AMI_PARAM" --region "$REGION" --query Parameter.Value --output text)
echo "ami: $AMI"

aws ec2 create-key-pair --key-name "$KEY" --region "$REGION" --query KeyMaterial --output text > "/tmp/$KEY"
chmod 600 "/tmp/$KEY"
SGID=$(aws ec2 create-security-group --group-name "$SGNAME" --description "lob probe" --region "$REGION" --query GroupId --output text)
MYIP=$(curl -s https://checkip.amazonaws.com)
aws ec2 authorize-security-group-ingress --group-id "$SGID" --protocol tcp --port 22 --cidr "${MYIP}/32" --region "$REGION" >/dev/null

IID=$(aws ec2 run-instances --image-id "$AMI" --instance-type "$TYPE" --key-name "$KEY" \
  --security-group-ids "$SGID" --region "$REGION" \
  --tag-specifications "ResourceType=instance,Tags=[{Key=Name,Value=$NAME}]" \
  --query 'Instances[0].InstanceId' --output text)
echo "instance: $IID (waiting for running…)"
aws ec2 wait instance-running --instance-ids "$IID" --region "$REGION"
IP=$(aws ec2 describe-instances --instance-ids "$IID" --region "$REGION" \
  --query 'Reservations[0].Instances[0].PublicIpAddress' --output text)
echo "ip: $IP"

echo "-- wait for ssh --"
for i in $(seq 1 40); do
  ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 \
      -i "/tmp/$KEY" "ec2-user@$IP" true >/dev/null 2>&1 && break
  sleep 5
done

echo "-- ship source + run --"
tar czf /tmp/lob-src.tgz -C "$LOB" include src tools scripts/aws
scp -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i "/tmp/$KEY" /tmp/lob-src.tgz "ec2-user@$IP:/tmp/"
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i "/tmp/$KEY" "ec2-user@$IP" \
  'sudo dnf install -y -q gcc-c++ perf tar git perl >/dev/null 2>&1; sudo sysctl -w kernel.perf_event_paranoid=-1 >/dev/null; mkdir -p /tmp/lob && tar xzf /tmp/lob-src.tgz -C /tmp/lob && sudo bash /tmp/lob/scripts/aws/run-probe.sh' \
  2>&1 | tee "$OUT"

echo "-- fetch flamegraph svgs --"
for bk in map ladder; do
  scp -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i "/tmp/$KEY" \
      "ec2-user@$IP:/tmp/fg-$bk.svg" "$LOB/scripts/aws/fg-${ARCH}-$bk.svg" 2>/dev/null \
      && echo "got fg-${ARCH}-$bk.svg" || echo "(no fg-$bk.svg)"
done

echo "== results saved: $OUT =="
