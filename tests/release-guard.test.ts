import { describe, expect, it } from "vitest";
import {
  checkReleaseTag,
  parseReleaseTag,
} from "../scripts/release-guard.mjs";

describe("release guard", () => {
  it("accepts a matching stable tag and selects the latest dist-tag", () => {
    expect(checkReleaseTag("v0.1.0", "0.1.0")).toEqual({
      version: "0.1.0",
      distTag: "latest",
    });
  });

  it("accepts a matching prerelease tag and selects the next dist-tag", () => {
    expect(checkReleaseTag("v0.1.0-rc.1", "0.1.0-rc.1")).toEqual({
      version: "0.1.0-rc.1",
      distTag: "next",
    });
  });

  it("rejects a tag that does not match the package version", () => {
    expect(() => checkReleaseTag("v9.9.9", "0.1.0")).toThrow(
      /v9\.9\.9 does not match package\.json version 0\.1\.0/,
    );
  });

  it("rejects a malformed tag", () => {
    expect(() => parseReleaseTag("v1.2")).toThrow(/invalid release tag/);
    expect(() => parseReleaseTag("1.2.3")).toThrow(/invalid release tag/);
    expect(() => parseReleaseTag("v1.2.3.4")).toThrow(/invalid release tag/);
  });
});
