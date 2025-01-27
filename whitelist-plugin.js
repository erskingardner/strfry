#!/usr/bin/env node

// Add your whitelisted pubkeys here
const whitelistedPubkeys = [
	"1739d937dc8c0c7370aa27585938c119e25c41f6c441a5d34c6d38503e3136ef",
	"fc16355b6d5dec84f053c9f35c4d53b8f55d18f25aa901365e5d5c3d1b3b5f38",
	"ee73f3642ebc2cfd10e5f67b285e06af5672416b4b916e9be38020ccdf5e1a84",
	"7a6ac2abc092d404a6a8ca93e97a58dc5082dfb8a744c984cd40c944fb6d6574",
];

// Event kinds that are allowed for non-whitelisted pubkeys
const ALLOWED_NON_WHITELIST_KINDS = [0, 3, 24133];

// Convert array to lookup object for better performance
const whitelist = Object.fromEntries(
	whitelistedPubkeys.map((key) => [key, true]),
);

const rl = require("node:readline").createInterface({
	input: process.stdin,
	output: process.stdout,
	terminal: false,
});

rl.on("line", (line) => {
	try {
		const req = JSON.parse(line);

		if (req.type !== "new") {
			console.error(`Unexpected request type: ${req.type}`);
			return;
		}

		const isPubkeyWhitelisted = whitelist[req.event.pubkey];
		const isAllowedKind = ALLOWED_NON_WHITELIST_KINDS.includes(req.event.kind);

		const response = {
			id: req.event.id,
			action: isPubkeyWhitelisted || isAllowedKind ? "accept" : "reject",
		};

		if (response.action === "reject") {
			response.msg = `Pubkey not in whitelist and/or event kind ${req.event.kind} not allowed`;
		}

		console.log(JSON.stringify(response));
	} catch (error) {
		console.error("Error processing event:", error);
	}
});
