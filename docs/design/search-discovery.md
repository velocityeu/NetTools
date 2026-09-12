# Search and AI discovery

Public author and publisher: **VEU**. Canonical website: https://velocityeu.github.io/NetTools/. This is an implementation and verification plan, not a claim that Google has indexed the website or will rank it in a particular position.

## What the site provides

The field guide has a static index at `/NetTools/help/` and one complete HTML page per lesson under `/NetTools/help/<id>/`. Its explanations, examples, reference links, breadcrumbs and previous/next navigation are present in the initial HTML. No JavaScript execution or button click is needed to read a lesson or discover the others. Google recommends ordinary anchors with `href` URLs for crawlable links. [Google link guidance](https://developers.google.com/search/docs/crawling-indexing/links-crawlable).

Each lesson has its own descriptive title, description, one main heading and self-referencing canonical URL. The sitemap lists the same canonical URLs, including the home page and field guide index. It omits update dates because the shared topic source does not record verified publication or revision dates. Keep these URLs stable; intentional future moves need redirects and updated internal links. [Google titles](https://developers.google.com/search/docs/appearance/title-link), [canonical URLs](https://developers.google.com/search/docs/crawling-indexing/consolidate-duplicate-urls), [sitemaps](https://developers.google.com/search/docs/crawling-indexing/sitemaps/build-sitemap).

JSON-LD describes the visible VEU organization, website, lesson and breadcrumb trail. The lessons use `TechArticle` and `LearningResource`; the index uses `CollectionPage`. There are no invented reviews, ratings, release dates, credentials or downloadable-software offers. Structured data must reflect visible content and does not guarantee a rich result. [Google structured-data policies](https://developers.google.com/search/docs/appearance/structured-data/sd-policies), [breadcrumb guidance](https://developers.google.com/search/docs/appearance/structured-data/breadcrumb).

## Google and AI features

Google's current guide says its generative AI search features build on the same search and quality foundations. Prioritize useful worked explanations, accurate calculations, clear authorship, accessible pages and links to authoritative references. Special AI markup is not required, and Google Search does not use `llms.txt`; this project therefore does not add one for Google discovery. These measures support access and understanding but cannot guarantee indexing, ranking or citation in Google or another AI service. [Google's AI optimization guide](https://developers.google.com/search/docs/fundamentals/ai-optimization-guide).

This GitHub Pages project controls `/NetTools/`, not the host root. A file at `/NetTools/robots.txt` would not control crawling of the site: Google requires the robots file at `https://velocityeu.github.io/robots.txt`. Do not create a project-path robots file and claim that it changes crawler access. Host-level policy needs whoever controls the organization site's root. [Google robots.txt location rules](https://developers.google.com/crawling/docs/robots-txt/create-robots-txt).

## Build and verification

The source remains `docs/help/topics.json`. Generate and check with:

```text
python scripts/build_help.py
python scripts/build_seo.py
python scripts/build_help.py --check
python scripts/build_seo.py --check
```

The SEO generator reuses the help validator and HTML renderer, then creates the lesson pages, index and `website-preview/dist/sitemap.xml`. It removes only obsolete lesson files carrying its own generated-file marker. `--check` changes nothing and fails when expected output is missing, outdated or obsolete. Do not edit generated pages by hand. The Pages workflow should run both freshness checks before preparing its complete site artifact.

After publication through the authorized VEU account or GitHub App, verify HTTP 200 responses, stylesheet/logo URLs, mobile layout, canonical URLs, lesson links and readability with JavaScript disabled. An authorized site owner can verify the URL-prefix property in Google Search Console, submit `https://velocityeu.github.io/NetTools/sitemap.xml`, inspect sample URLs and monitor indexing/search performance. Ownership verification, sitemap submission and search-engine indexing have not been performed by this implementation. Use Google's Rich Results Test and the Schema.org validator to inspect deployed structured data; a passing result is not a ranking promise. [Google sitemap submission](https://developers.google.com/search/docs/crawling-indexing/sitemaps/build-sitemap), [Google Rich Results Test](https://search.google.com/test/rich-results), [Schema.org validator](https://validator.schema.org/).

Publishing remains separate from local generation. Do not push, deploy, submit ownership-verification changes or call mutating GitHub APIs using a personal login. Public publisher and author identity must remain VEU.
