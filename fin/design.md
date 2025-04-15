# Finance App Design

## Goals

The goal of this project is to provide easy analytics for Vanguard and other accounts.  We want to be able to report total returns and dividend returns for any subset of the investment funds.  We want to be able to recommend which lots should be sold to minimize taxes.  We want to be able to estimate the results on a portfolio of events like the 2008 crash and the like.

Architecturally we would also like the resulting system to be extensible and usable as a service.  Although initially we expect to use a web server simply as a user interface, we'd like to extend it to an actual web service.  This means we'd like to implement a number of features:

 1. Extensible -- someone should be able to define new functionality to the app, and have it show up in the user interface.  The extension should be able to read and perhaps augment the transaction data.  The extension should be safe

## Data Structures
There are two important sets of data that we track per fund per account: the transactions and balance file, and the tax lot file.

For any given fund, we know the current balance, and we have, at least with Vanguard, about 18 months of transactions describing buy, sell, dividend, long and short term capital gains distributions, and splits.

From this information, we want to be able to compute dividend and total returns.  That means that after each transaction, we want to be able to compute the fund balance, along with any gain or loss not reflected in changes to the share price or balance.  So, each transaction should have a date, a dollar amount, an optional associated gain or loss, an optional share price, and an associated fund balance.

## Extension Architectures
We can imagine several different types of extension architectures.

One easy one would be the **Python Extension** approach: define a Python API for defining new commands and graph agents, and use Python's importlib function to load it directly into the application.  This could work pretty well for a stand-alone application, but has obvious security issues if you talk about uploading code to implement new functionality to a web server, especially one shared among multiple users.

Another alternative would be to implement a secure scripting language, call it **Argo** (for Argonaut), that could access data in JSON tables.  Perhaps it could have a trivial Lisp-like syntax, with data operations that understand JSON arrays, dictionaries and variables.

A third alternative, **ClientApp**, is more flexible and more complex than the above.  It would be to allow a user to define a server receiving calls from the app web server.  It could upcall to the server to pick up requests and to deliver responses so that no router work would be necessary at the client.  This would also allow a programmatic interface for loading customer data to run without ever sending credentials to a potentially untrusted web server.

The hacker in me likes the ClientApp approach but that approach requires a sizable investment in infrastructure to be able to do anything at all.  The **Python Extension** model has pretty serious issues with security that are probably irremediable.  That leaves **JLisp** as the likely approach for extending the application with new data analyzing commands.

## Algorithmic Sketch
This section describe the various key algorithms used by this application.

### Record Processing
This section describes how we process the various records in the transaction file.

In the following, we assume that we have some idea of the cost of various shares in the fund.  This may come from one of three places:

 1. Lot information exported from the account as of a certain date.
 2. The total cost of shares from the account as of a certain date
 3. Zero, or a manually entered number as of the first transaction.

**Buy** -- these records simply increase the share balance.
**Sell** -- these records reduce the share balance, and record the gain or loss in this record, computed from the Lot selection criteria and the share cost data described above.  These records essentially lock in a gain or loss for some shares as of the transaction date.
**Dividend** -- these records represent a dividend paid, and represent pure gain at the date they were paid.  If they are reinvested in the original fund, they also represent an associated Buy operation, but the Buy operation doesn't have any effect on the actual recorded return.
**Capital Gains Distribution** -- Both long and short term distributions represent essentially a Sell operation potentially followed by a Buy if the CG is reinvested in the original fund.  So a typical capital gains distribution appears as a reduction in share price for the fund combined with a cash gain.

This should be return neutral, so the cash gain is recorded as a gain at this date.  It will be associated with a Buy operation in whatever fund the distribution is made; if it is a reinvested capital gain, it will be represented as additional purchased shares in the original fund, and otherwise it will be represented as additional shares in some other fund.

### Computing Returns
How do we compute actual returns between two dates?  There are two alternatives, one weighted by the actual amount of a fund that was owned, and one representing the actual observed return weighted only by duration.  We will describe the latter.

The transactions file contains records with a date, a starting balance, ending balance, and a specific gain.  For each record after the first, we compute a duration as the time between this record and the previous, and a gain or loss based on the difference in value between the previous record's ending balance and this record's starting balance, augmented with the specific gain of the second record.  Then for each duration range, take the log of the gain * the duration in years, and sum these up for the entire range.  Divide by the duration in years, and take the inverse log of the result and that's the annual yield.  If a particular range is only partially included in our target range, linearly scale the range and log of the gain.

### Maintaining Transaction Records 

### Maintaining Tax Lots
We can maintain 

## Drivers

For Vanguard, it would be nice if we could use OFX, but their OFX server is currently non-functional.  But we should define a clean data provider interface in Python that allows tax lot, account information and transaction information to be provided to the web service.

We can download tax lot information, as well as current balance and transaction information as follows:

**Download OfxDownload.csv:**
**https://personal1.vanguard.com/ofu-open-fin-exchange-webapp/ofx-welcome**

**from Portfolio / Tax Basis**
**https://cost-basis.web.vanguard.com/unrealized**

