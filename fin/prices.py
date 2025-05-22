import sys
import yfinance as yf

class History:
    def __init__():
        self.prices = [];


def next_month(date):
    comps = date.split('-')
    cint = []
    cint.append(int(comps[0]))
    cint.append(int(comps[1]))
    cint.append(int(comps[2]))
    cint[1] = cint[1] + 1
    if (cint[1] == 13):
        cint[1] = 1
        cint[0] = cint[0] + 1
    return f"{cint[0]:04}-{cint[1]:02}-{cint[2]:02}"

def next_year(date):
    comps = date.split('-')
    cint = []
    cint.append(int(comps[0]) + 1)
    cint.append(1)
    cint.append(1)
    return f"{cint[0]:04}-{cint[1]:02}-{cint[2]:02}"

def main():
    if len(sys.argv) < 3:
        print("usage: prices prices date symbol1")
        print("usage: prices yield symbol1")
        print("date format is YYYY-MM-DD")
        return
    if sys.argv[1] == "prices":
        ticker = yf.Ticker(sys.argv[3])
        start_date = sys.argv[2]
        end_date = next_year(start_date)
        hist = ticker.history(start=start_date, end=end_date)
        rlist = hist['Close'].tolist()
        dlist = hist.index.tolist()
        count = len(rlist)
        if len(rlist) > 0:
            for date, price in zip(dlist, rlist):
                print(f"{date.year:4}-{date.month:02}-{date.day:02} {price}")
            print("DONE")
    elif sys.argv[1] == "yield":
        ticker = yf.Ticker(sys.argv[2])
        stock_dict = ticker.info
        y = stock_dict.get("yield")
        if y == None:
            y = stock_dict.get("dividendYield")
            if y == None:
                print("None")
            else:
                y = y / 100.0
                print(y)
        else:
            print(y)

if __name__ == "__main__":
    main()
