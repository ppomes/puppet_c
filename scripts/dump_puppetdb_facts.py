#!/usr/bin/env python3
"""
Dump all facts from PuppetDB to allfacts.yaml format for use with puppetc-compile.

Usage:
    # Local PuppetDB (no SSL)
    ./dump_puppetdb_facts.py -H localhost -p 8080 -o allfacts.yaml

    # Remote PuppetDB with SSL
    ./dump_puppetdb_facts.py -H puppetdb.example.com -p 8081 --ssl \
        --cert /etc/puppetlabs/puppet/ssl/certs/$(hostname -f).pem \
        --key /etc/puppetlabs/puppet/ssl/private_keys/$(hostname -f).pem \
        --cacert /etc/puppetlabs/puppet/ssl/certs/ca.pem \
        -o allfacts.yaml

    # Filter specific nodes
    ./dump_puppetdb_facts.py -H localhost -p 8080 --query '["~", "certname", "prod"]'
"""

import argparse
import json
import sys
import urllib.request
import urllib.error
import ssl

def query_puppetdb(host, port, use_ssl=False, cert=None, key=None, cacert=None, query=None):
    """Query PuppetDB inventory endpoint."""
    protocol = "https" if use_ssl else "http"
    url = f"{protocol}://{host}:{port}/pdb/query/v4/inventory"

    if query:
        url += f"?query={urllib.parse.quote(query)}"

    # Set up SSL context if needed
    context = None
    if use_ssl:
        context = ssl.create_default_context()
        if cacert:
            context.load_verify_locations(cacert)
        if cert and key:
            context.load_cert_chain(cert, key)

    request = urllib.request.Request(url)
    request.add_header('Accept', 'application/json')

    try:
        with urllib.request.urlopen(request, context=context, timeout=300) as response:
            return json.load(response)
    except urllib.error.HTTPError as e:
        print(f"HTTP Error {e.code}: {e.reason}", file=sys.stderr)
        sys.exit(1)
    except urllib.error.URLError as e:
        print(f"Connection error: {e.reason}", file=sys.stderr)
        sys.exit(1)

def convert_to_allfacts(inventory_data):
    """Convert PuppetDB inventory format to allfacts.yaml format."""
    facts = {}
    for node in inventory_data:
        certname = node.get("certname")
        node_facts = node.get("facts", {})
        if certname and node_facts:
            facts[certname] = node_facts
    return {"facts": facts}

def output_yaml(data, output_file=None):
    """Output data as YAML."""
    try:
        import yaml
        yaml_str = yaml.dump(data, default_flow_style=False, allow_unicode=True, sort_keys=True)
    except ImportError:
        # Fallback: simple YAML-like output without pyyaml
        print("Warning: PyYAML not installed, using JSON output", file=sys.stderr)
        yaml_str = json.dumps(data, indent=2)

    if output_file:
        with open(output_file, 'w') as f:
            f.write(yaml_str)
        print(f"Written {len(data.get('facts', {}))} nodes to {output_file}", file=sys.stderr)
    else:
        print(yaml_str)

def main():
    parser = argparse.ArgumentParser(description='Dump PuppetDB facts to allfacts.yaml format')
    parser.add_argument('-H', '--host', default='localhost', help='PuppetDB hostname')
    parser.add_argument('-p', '--port', type=int, default=8080, help='PuppetDB port')
    parser.add_argument('--ssl', action='store_true', help='Use HTTPS')
    parser.add_argument('--cert', help='Client certificate file')
    parser.add_argument('--key', help='Client key file')
    parser.add_argument('--cacert', help='CA certificate file')
    parser.add_argument('-o', '--output', help='Output file (default: stdout)')
    parser.add_argument('-q', '--query', help='PQL query to filter nodes')
    parser.add_argument('--json', action='store_true', help='Output as JSON instead of YAML')

    args = parser.parse_args()

    print(f"Querying PuppetDB at {args.host}:{args.port}...", file=sys.stderr)

    inventory = query_puppetdb(
        host=args.host,
        port=args.port,
        use_ssl=args.ssl,
        cert=args.cert,
        key=args.key,
        cacert=args.cacert,
        query=args.query
    )

    print(f"Retrieved facts for {len(inventory)} nodes", file=sys.stderr)

    allfacts = convert_to_allfacts(inventory)

    if args.json:
        if args.output:
            with open(args.output, 'w') as f:
                json.dump(allfacts, f, indent=2)
        else:
            print(json.dumps(allfacts, indent=2))
    else:
        output_yaml(allfacts, args.output)

if __name__ == '__main__':
    main()
