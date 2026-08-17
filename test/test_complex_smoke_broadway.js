const { chromium } = require("playwright");

const url = process.env.GAWM_SMOKE_URL;
const screenshot = process.env.GAWM_SMOKE_SCREENSHOT;
const chromiumPath = process.env.GAWM_SMOKE_CHROMIUM || "/usr/bin/chromium";

if (!url || !screenshot) {
  throw new Error("GAWM_SMOKE_URL and GAWM_SMOKE_SCREENSHOT are required");
}

(async () => {
  const browser = await chromium.launch({
    headless: true,
    executablePath: chromiumPath,
  });

  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    await page.goto(url, { waitUntil: "networkidle" });

    const canvas = page.locator("canvas");
    await canvas.waitFor({ state: "visible" });
    await page.waitForTimeout(1000);

    const box = await canvas.boundingBox();
    if (!box || box.width < 600 || box.height < 300) {
      throw new Error(`unexpected Broadway canvas geometry: ${JSON.stringify(box)}`);
    }

    // Coordinates are relative to Broadway's canvas with gawm's default
    // preferences. Expand the file root, then the complex V(out) folder.
    await page.mouse.click(box.x + 100, box.y + 189);
    await page.waitForTimeout(300);
    await page.mouse.click(box.x + 100, box.y + 211);
    await page.waitForTimeout(500);

    await page.screenshot({ path: screenshot });
    console.log(`Broadway screenshot: ${screenshot}`);
  } finally {
    await browser.close();
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
