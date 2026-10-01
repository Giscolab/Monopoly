// USA/en-US printed card bodies, in IBarCardPlayback native 0..31 order.
// Verified against production-decoded licensed 400x240 idle face bitmaps.
// Occluded words on Chance3/Community4/6/7/9 are completed from the exact
// associated PB WAV comments in Source/monopoly/UDPenny.h:205-236 (read-only).
// Case/punctuation are normalized for modern typography; effects are unchanged.
constexpr std::array<std::string_view,32> ModernUsaDrawCardBodies{
    "Advance to GO. (Collect $200)",
    "Advance to St. Charles Place. If you pass GO, collect $200.",
    "Advance token to nearest Utility. If unowned you may buy it from bank. If owned, throw dice and pay owner a total ten times the amount thrown.",
    "Your building and loan matures. Collect $150.",
    "Bank pays you dividend of $50.",
    "You have been elected Chairman of the Board. Pay each player $50.",
    "Take a walk on the Board Walk. Advance token to Board Walk.",
    "Take a ride on the Reading. If you pass GO, collect $200.",
    "Advance token to the nearest Railroad and pay owner twice the Rental to which he is otherwise entitled. If Railroad is unowned, you may buy it from the Bank.",
    "Advance token to the nearest Railroad and pay owner twice the Rental to which he is otherwise entitled. If Railroad is unowned, you may buy it from the Bank.",
    "Make general repairs on all your property. For each house pay $25. For each hotel $100.",
    "This card may be kept until needed or sold. Get out of jail free.",
    "Pay poor tax of $15.",
    "Advance to Illinois Ave.",
    "Go directly to jail. Do not pass GO. Do not collect $200.",
    "Go back 3 spaces.",
    "Life insurance matures. Collect $100.",
    "You inherit $100.",
    "Xmas fund matures. Collect $100.",
    "You have won second prize in a beauty contest. Collect $10.",
    "Get out of jail, free. This card may be kept until needed or sold.",
    "Doctor's fee. Pay $50.",
    "Bank error in your favour. Collect $200.",
    "Income tax refund. Collect $20.",
    "You are assessed for street repairs. $40 per house. $115 per hotel.",
    "Go to jail. Go directly to jail. Do not pass GO. Do not collect $200.",
    "From sale of stock you get $45.",
    "Grand Opera Opening. Collect $50 from every player for opening night seats.",
    "Advance to GO. (Collect $200)",
    "Pay school tax of $150.",
    "Receive for services $25.",
    "Pay hospital $100."
};
