"""Independent standard-library oracle for every IPv4/IPv6 prefix boundary."""
import ipaddress
import json
fixtures=[]
for literal in ['203.0.113.197','255.255.255.255','2001:db8:abcd:1234:5678:9abc:def0:1234','ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff']:
    address=ipaddress.ip_address(literal)
    for prefix in range(address.max_prefixlen+1):
        value=ipaddress.ip_network(f'{literal}/{prefix}',strict=False)
        fixtures.append({'address':f'{literal}/{prefix}','network':str(value),'last':str(value.broadcast_address),'total':str(value.num_addresses)})
print(json.dumps(fixtures))
